#include <JuceHeader.h>
#include "ButterEngine.h"
#include "LoudnessMatch.h"
#include <atomic>

class ButterProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    ButterProcessor() : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        state(*this, nullptr, "ButterState", layout()) { startTimerHz(10); }
    ~ButterProcessor() override { stopTimer(); }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
        juce::AudioProcessorValueTreeState::ParameterLayout p;
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"compress",1}, "Compress", 0.f, 1.f, 0.f));
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"output",1}, "Output", 0.f, 2.f, 1.f));
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix",1}, "Mix", 0.f, 1.f, 1.f));
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"matchEnabled",1}, "Level Match", true));
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"original",1}, "Original A/B", false));
        return p;
    }
    void prepareToPlay(double rate, int) override {
        engine.reset(rate); reductionL=0; reductionR=0; loudness.prepare(rate);
        learning=false; progress=0; command=0;
        gainSmooth.reset(rate,.02); blendSmooth.reset(rate,.005);
        gainSmooth.setCurrentAndTargetValue(targetGain());
        blendSmooth.setCurrentAndTargetValue(state.getRawParameterValue("original")->load()>.5f?0.:1.);
    }
    double targetGain() const {
        return hasMatch.load() && state.getRawParameterValue("matchEnabled")->load()>.5f
            ? std::pow(10.,static_cast<double>(matchDb.load())/20.) : 1.;
    }
    void finishLearn() {
        const auto result=loudness.finish();
        if(result==LoudnessMatch::Result::success || result==LoudnessMatch::Result::limited) {
            matchDb=static_cast<float>(loudness.trimDb); hasMatch=true;
            learnedInput=static_cast<float>(loudness.inputLufs); learnedOutput=static_cast<float>(loudness.outputLufs);
            matchRevision.fetch_add(1);
        }
        learnResult=static_cast<int>(result); learning=false;
    }
    void handleCommand() {
        const auto request=command.exchange(0);
        if(request==1) { loudness.start(); learning=true; progress=0; learnResult=0; }
        if(request==2 && learning.load()) finishLearn();
        if(request==4) { loudness.cancel(); learning=false; progress=0; learnResult=0; }
        if(request==3) {
            loudness.cancel(); learning=false; progress=0; hasMatch=false; matchDb=0; learnResult=0;
            learnedInput=0; learnedOutput=0;
        }
    }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    template<class T> void process(juce::AudioBuffer<T>& b) {
        juce::ScopedNoDenormals guard;
        engine.parameters(state.getRawParameterValue("compress")->load(),
                          state.getRawParameterValue("output")->load() * .5f,
                          state.getRawParameterValue("mix")->load());
        handleCommand();
        // Small stack buffers keep the original input available without audio-thread allocations.
        // Both measurements precede the compensation stage; no gain feeds back into ButterComp2.
        std::array<T,256> dryL{},dryR{};
        for(int offset=0;offset<b.getNumSamples();offset+=256) {
            const int count=juce::jmin(256,b.getNumSamples()-offset);
            auto* left=b.getWritePointer(0)+offset; auto* right=b.getWritePointer(1)+offset;
            std::copy_n(left,count,dryL.data()); std::copy_n(right,count,dryR.data());
            T* data[]={left,right};
            if constexpr (std::is_same_v<T,float>) engine.processReplacing(data,data,count);
            else engine.processDoubleReplacing(data,data,count);
            gainSmooth.setTargetValue(targetGain());
            blendSmooth.setTargetValue(state.getRawParameterValue("original")->load()>.5f?0.:1.);
            for(int i=0;i<count;++i) {
                if(loudness.learning()) {
                    loudness.sample(dryL[static_cast<size_t>(i)],dryR[static_cast<size_t>(i)],left[i],right[i]);
                    if(loudness.complete()) { finishLearn(); gainSmooth.setTargetValue(targetGain()); }
                }
                const auto gain=gainSmooth.getNextValue(),blend=blendSmooth.getNextValue();
                // Preserve the original sample exactly at either unity endpoint.
                if(blend<=0) { left[i]=dryL[static_cast<size_t>(i)]; right[i]=dryR[static_cast<size_t>(i)]; }
                else if(blend>=1) { if(gain<1 || gain>1) { left[i]=static_cast<T>(left[i]*gain); right[i]=static_cast<T>(right[i]*gain); } }
                else {
                    left[i]=static_cast<T>(dryL[static_cast<size_t>(i)]*(1-blend)+left[i]*gain*blend);
                    right[i]=static_cast<T>(dryR[static_cast<size_t>(i)]*(1-blend)+right[i]*gain*blend);
                }
            }
        }
        progress=loudness.seconds();
        publishMeters();
    }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override { process(b); }
    void processBlock(juce::AudioBuffer<double>& b, juce::MidiBuffer&) override { process(b); }
    void publishMeters() {
        reductionL.store(static_cast<float>(engine.reductionDb(0)), std::memory_order_relaxed);
        reductionR.store(static_cast<float>(engine.reductionDb(1)), std::memory_order_relaxed);
        meterSequence.fetch_add(1, std::memory_order_relaxed);
    }
    void processBlockBypassed(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        handleCommand(); engine.releaseMeters(b.getNumSamples()); publishMeters();
    }
    void processBlockBypassed(juce::AudioBuffer<double>& b, juce::MidiBuffer&) override {
        handleCommand(); engine.releaseMeters(b.getNumSamples()); publishMeters();
    }
    bool supportsDoublePrecisionProcessing() const override { return true; }
    const juce::String getName() const override { return "ButterComp Studio"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock& dst) override {
        auto saved=state.copyState();
        saved.setProperty("matchDb",matchDb.load(),nullptr);
        saved.setProperty("hasMatch",hasMatch.load(),nullptr);
        saved.setProperty("learnedInput",learnedInput.load(),nullptr);
        saved.setProperty("learnedOutput",learnedOutput.load(),nullptr);
        if(auto xml=saved.createXml()) copyXmlToBinary(*xml,dst);
    }
    void setStateInformation(const void* data,int size) override {
        if(auto xml=getXmlFromBinary(data,size); xml && xml->hasTagName(state.state.getType())) {
            auto restored=juce::ValueTree::fromXml(*xml);
            const auto db=static_cast<float>(restored.getProperty("matchDb",0.f));
            matchDb=std::isfinite(db)?juce::jlimit(-24.f,24.f,db):0.f;
            hasMatch=std::isfinite(db)&&static_cast<bool>(restored.getProperty("hasMatch",false));
            learnedInput=static_cast<float>(restored.getProperty("learnedInput",0.f));
            learnedOutput=static_cast<float>(restored.getProperty("learnedOutput",0.f));
            command=4; // Cancel in-flight learning on the audio thread after state restoration.
            // Legacy sessions predate both appended controls; restore their defaults.
            for(const auto& entry : std::array<std::pair<const char*,float>,2>{{{"matchEnabled",1.f},{"original",0.f}}}) {
                if(!restored.getChildWithProperty("id",entry.first).isValid()) {
                    juce::ValueTree parameter{"PARAM"};
                    parameter.setProperty("id",entry.first,nullptr); parameter.setProperty("value",entry.second,nullptr);
                    restored.appendChild(parameter,nullptr);
                }
            }
            state.replaceState(restored);
        }
    }
    juce::AudioProcessorValueTreeState state;
    std::atomic<float> reductionL{0}, reductionR{0};
    std::atomic<uint32_t> meterSequence{0};
    std::atomic<float> matchDb{0},progress{0},learnedInput{0},learnedOutput{0};
    std::atomic<bool> hasMatch{false},learning{false};
    std::atomic<int> command{0},learnResult{0};
    std::atomic<uint32_t> matchRevision{0};
private:
    void timerCallback() override {
        // Mark custom learned state dirty on the message thread, even if the
        // editor closed during learning. Never call host listeners from process().
        const auto revision=matchRevision.load();
        if(revision!=notifiedRevision) {
            notifiedRevision=revision;
            updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
        }
    }
    uint32_t notifiedRevision=0;
    ButterEngine engine;
    LoudnessMatch loudness;
    juce::SmoothedValue<double> gainSmooth{1.},blendSmooth{1.};
};

namespace style {
const juce::Colour cream{0xfff4eddb}, ink{0xff252b29}, gold{0xffd7ac61}, muted{0xff858b80};
class Look final : public juce::LookAndFeel_V4 {
public:
    Look() {
        setColour(juce::Slider::textBoxTextColourId, cream);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::highlightColourId, gold.withAlpha(.4f));
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float start,float end,juce::Slider&) override {
        auto r=juce::Rectangle<float>(static_cast<float>(x),static_cast<float>(y),static_cast<float>(w),static_cast<float>(h)).reduced(16);
        auto radius=juce::jmin(r.getWidth(),r.getHeight())*.5f;
        auto cx=r.getCentreX(), cy=r.getCentreY();
        juce::Path track, active;
        track.addCentredArc(cx,cy,radius,radius,0,start,end,true);
        active.addCentredArc(cx,cy,radius,radius,0,start,start+pos*(end-start),true);
        g.setColour(cream.withAlpha(.12f)); g.strokePath(track,juce::PathStrokeType(3));
        g.setColour(gold); g.strokePath(active,juce::PathStrokeType(3));
        auto dial=juce::Rectangle<float>(radius*1.58f,radius*1.58f).withCentre({cx,cy});
        g.setColour(juce::Colours::black.withAlpha(.3f)); g.fillEllipse(dial.translated(0,5));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff535a52),cx,cy-radius,ink,cx,cy+radius,false));
        g.fillEllipse(dial); g.setColour(cream.withAlpha(.15f)); g.drawEllipse(dial,1);
        const auto angle=start+pos*(end-start);
        g.setColour(gold);
        g.drawLine(cx+std::sin(angle)*radius*.48f,cy-std::cos(angle)*radius*.48f,
                   cx+std::sin(angle)*radius*.68f,cy-std::cos(angle)*radius*.68f,3);
    }
};
}

class ReductionMeter final : public juce::Component {
public:
    explicit ReductionMeter(juce::String channelName) : channel(std::move(channelName)) {
        setTitle("Gain reduction " + channel);
        setDescription("Compressor gain reduction in decibels, before output gain and dry/wet mix");
        setAccessible(true);
    }
    void setReduction(float db) {
        value = juce::jlimit(0.f, 30.f, db);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colours::black.withAlpha(.4f));
        g.fillRoundedRectangle(bounds.reduced(1).translated(0,3),12);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff777d70),0,0,juce::Colour(0xff151b18),0,bounds.getHeight(),false));
        g.fillRoundedRectangle(bounds,12);
        auto face = bounds.reduced(7);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffff4cf),face.getCentreX(),face.getCentreY(),juce::Colour(0xffcfbb8b),face.getRight(),face.getBottom(),true));
        g.fillRoundedRectangle(face,7);
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(face.toNearestInt());
        const float cx=bounds.getCentreX(), cy=bounds.getHeight()-29.f;
        const float radius=bounds.getWidth()*.385f;
        auto angleFor = [](float db) {
            // Nonlinear calibration devotes more travel to the useful 0-10 dB range.
            return juce::jmap(std::log1p(db)/std::log(31.f), .94f, -.94f);
        };
        auto point = [cx,cy](float angle,float distance) {
            return juce::Point<float>{cx+std::sin(angle)*distance,cy-std::cos(angle)*distance};
        };
        juce::Path arc;
        arc.addCentredArc(cx,cy,radius,radius,0,angleFor(30),angleFor(0),true);
        g.setColour(juce::Colour(0xff5a5141)); g.strokePath(arc,juce::PathStrokeType(1));
        for(int db=0;db<=30;++db) {
            const bool major=db==0||db==1||db==2||db==3||db==5||db==7||db==10||db==15||db==20||db==30;
            if(!major && db<10) continue;
            const auto angle=angleFor(static_cast<float>(db));
            const auto a=point(angle,radius), b=point(angle,radius+(major?10.f:5.f));
            g.setColour(db>=15?juce::Colour(0xffa44f3e):juce::Colour(0xff514a3d));
            g.drawLine({a,b},major?1.3f:.7f);
            if(major) {
                auto textCentre=point(angle,radius+22);
                g.setFont(juce::FontOptions(11.f));
                g.drawText(juce::String(db),juce::Rectangle<float>(26,16).withCentre(textCentre),juce::Justification::centred);
            }
        }
        g.setColour(juce::Colour(0xff706249)); g.setFont(juce::FontOptions(10.f));
        g.drawText("GAIN REDUCTION",0,static_cast<int>(cy-47),getWidth(),16,juce::Justification::centred);
        g.setFont(juce::FontOptions(12.f).withStyle("Bold"));
        g.drawText(channel + "  /  dB",0,static_cast<int>(cy-28),getWidth(),16,juce::Justification::centred);
        auto tip=point(angleFor(value),radius+7);
        g.setColour(juce::Colours::black.withAlpha(.16f)); g.drawLine(cx+2,cy+2,tip.x+2,tip.y+2,3);
        g.setColour(juce::Colour(0xffad4234)); g.drawLine(cx,cy,tip.x,tip.y,2);
        g.setColour(juce::Colour(0xff484639)); g.fillEllipse(cx-6,cy-6,12,12);
        g.setColour(juce::Colour(0xffb6aa88)); g.fillEllipse(cx-2,cy-2,4,4);
        // Small numerical readout supports precise setting alongside the needle.
        g.setColour(juce::Colour(0xff675941)); g.setFont(juce::FontOptions(10.f));
        g.drawText(juce::String(value,1)+" dB",getWidth()-68,getHeight()-28,49,15,juce::Justification::right);
        // Soft reflection on the meter glass.
        juce::Path reflection;
        reflection.addTriangle(face.getX(),face.getY(),face.getRight(),face.getY(),face.getX(),face.getY()+face.getHeight()*.42f);
        g.setColour(juce::Colours::white.withAlpha(.14f)); g.fillPath(reflection);
    }
private:
    juce::String channel;
    float value=0;
};

class ButterEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ButterEditor(ButterProcessor& proc) : AudioProcessorEditor(proc), processor(proc) {
        setLookAndFeel(&look);
        const char* ids[]={"compress","output","mix"};
        const char* labels[]={"COMPRESS","OUTPUT","MIX"};
        for(int i=0;i<3;++i) {
            auto& s=knobs[static_cast<size_t>(i)];
            addAndMakeVisible(s);
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,24);
            s.setName(labels[i]);
            s.setTooltip(i==0 ? "ButterComp2 compression amount. Double-click to reset." :
                         i==1 ? "Linear output gain, 0-2x. Double-click for unity." : "Blend dry and compressed audio. Double-click for fully wet.");
            attachments[static_cast<size_t>(i)]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,ids[i],s);
            s.textFromValueFunction=[i](double v) { return juce::String(i==1?v:v*100,i==1?2:0)+(i==1?" x":" %"); };
            s.valueFromTextFunction=[i](const juce::String& text) { return text.getDoubleValue()/(i==1?1:100); };
            s.updateText();
            s.setDoubleClickReturnValue(true,i==0?0.:1.);
        }
        addAndMakeVisible(leftMeter);
        addAndMakeVisible(rightMeter);
        shownL=processor.reductionL.load(std::memory_order_relaxed);
        shownR=processor.reductionR.load(std::memory_order_relaxed);
        lastSequence=processor.meterSequence.load(std::memory_order_relaxed);
        leftMeter.setReduction(shownL); rightMeter.setReduction(shownR);
        for(auto* button : {&learnButton,&resetButton,&enableButton,&abButton}) {
            addAndMakeVisible(*button);
            button->setColour(juce::TextButton::buttonColourId,style::ink.brighter(.15f));
            button->setColour(juce::TextButton::textColourOffId,style::cream);
        }
        learnButton.setComponentID("learnMatch"); resetButton.setComponentID("resetMatch");
        enableButton.setComponentID("matchEnabled"); abButton.setComponentID("abOriginal");
        readout.setComponentID("matchReadout");
        learnButton.onClick=[this] { processor.command=processor.learning.load()?2:1; };
        resetButton.onClick=[this] {
            processor.hasMatch=false; processor.matchDb=0; processor.learning=false;
            processor.progress=0; processor.learnResult=0; processor.command=3; processor.matchRevision.fetch_add(1); refreshMatch();
        };
        enableButton.setClickingTogglesState(true); abButton.setClickingTogglesState(true);
        enableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,"matchEnabled",enableButton);
        abAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,"original",abButton);
        addAndMakeVisible(readout); readout.setJustificationType(juce::Justification::centred);
        readout.setColour(juce::Label::textColourId,style::gold);
        readout.setFont(juce::FontOptions(26.f).withStyle("Bold"));
        addAndMakeVisible(matchStatus); matchStatus.setColour(juce::Label::textColourId,style::cream);
        matchStatus.setFont(juce::FontOptions(11.f)); matchStatus.setJustificationType(juce::Justification::centred);
        resetButton.setTooltip("Clear only the learned adjustment. Compress, Output, Mix and A/B remain unchanged.");
        enableButton.setTooltip("Enable or disable the learned gain without losing it.");
        abButton.setTooltip("Compare the original input with the processed output. Short fades prevent switching clicks.");
        refreshMatch();
        setSize(840,810);
        startTimerHz(30);
    }
    ~ButterEditor() override { stopTimer(); setLookAndFeel(nullptr); }
    void resized() override {
        leftMeter.setBounds(43,141,361,208);
        rightMeter.setBounds(436,141,361,208);
        knobs[0].setBounds(80,402,230,207);
        knobs[1].setBounds(365,423,160,174);
        knobs[2].setBounds(594,423,160,174);
        learnButton.setBounds(44,652,148,34); resetButton.setBounds(204,652,126,34);
        enableButton.setBounds(44,698,148,32); abButton.setBounds(204,698,126,32);
        readout.setBounds(353,645,432,46); matchStatus.setBounds(345,694,448,48);
    }
    void paint(juce::Graphics& g) override {
        using namespace style;
        g.fillAll(cream);
        g.setColour(ink); g.setFont(juce::FontOptions(11.f));
        g.drawText("YODO AUDIO    /    AIRWINDOWS DSP",32,20,600,20,juce::Justification::left);
        g.setFont(juce::FontOptions(39.f)); g.drawText("ButterComp",30,45,450,54,juce::Justification::left);
        g.setFont(juce::FontOptions(13.f)); g.drawText("S T U D I O",635,65,170,24,juce::Justification::right);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff343d36),0,115,juce::Colour(0xff1e2722),0,775,false));
        g.fillRoundedRectangle(16,114,808,661,16);
        // Hairline trim and recessed screws give the panel a hardware finish.
        g.setColour(gold.withAlpha(.2f)); g.drawRoundedRectangle(juce::Rectangle<float>(17,115,806,659),15,1);
        for(float x:{30.f,810.f}) for(float y:{128.f,761.f}) {
            g.setColour(juce::Colour(0xff111a15)); g.fillEllipse(x-4,y-4,8,8);
            g.setColour(juce::Colour(0xff646c5b)); g.drawLine(x-2,y+1,x+2,y-1,1);
        }
        g.setColour(gold); g.setFont(juce::FontOptions(10.f));
        g.drawText("WET COMPRESSION   /   BEFORE OUTPUT & MIX",43,358,754,20,juce::Justification::centred);
        g.setColour(cream.withAlpha(.12f)); g.drawHorizontalLine(388,44,796);
        g.setColour(gold); g.setFont(juce::FontOptions(12.f));
        g.drawText("COMPRESS",80,399,230,24,juce::Justification::centred);
        g.drawText("OUTPUT",365,412,160,24,juce::Justification::centred);
        g.drawText("MIX",594,412,160,24,juce::Justification::centred);
        g.setColour(cream.withAlpha(.1f)); g.drawVerticalLine(330,417,595);
        g.setColour(cream.withAlpha(.12f)); g.drawHorizontalLine(626,44,796);
        g.setColour(gold); g.setFont(juce::FontOptions(10.f));
        g.drawText("LUFS LEVEL MATCH",44,605,752,20,juce::Justification::centred);
        g.setColour(ink.withAlpha(.65f)); g.setFont(juce::FontOptions(11.f));
        g.drawText("ButterComp2 by Chris Johnson  /  Independent studio edition",32,781,700,22,juce::Justification::left);
        g.drawText("02",745,781,60,22,juce::Justification::right);
    }
private:
    void refreshMatch() {
        const bool learned=processor.hasMatch.load(),active=processor.learning.load();
        const float db=processor.matchDb.load();
        const auto trim=(db>=0?juce::String("+"):juce::String{})+juce::String(db,1)+" dB";
        readout.setText(learned?"MATCH  "+trim:"MATCH  -- dB",juce::dontSendNotification);
        learnButton.setButtonText(active?"Finish Learning":"Learn Match");
        enableButton.setButtonText(enableButton.getToggleState()?"Match ON":"Match OFF");
        abButton.setButtonText(abButton.getToggleState()?"A: Original":"B: Processed");
        juce::String text;
        if(active) text="Learning: "+juce::String(processor.progress.load(),1)+" / 15 s of audio";
        else if(processor.learnResult.load()==static_cast<int>(LoudnessMatch::Result::tooShort)) text="Not enough audio. Play at least 3 seconds and retry.";
        else if(processor.learnResult.load()==static_cast<int>(LoudnessMatch::Result::noOutput)) text="No measurable output. Check Output / Mix and retry.";
        else if(learned) {
            text="Input "+juce::String(processor.learnedInput.load(),1)+"  /  Processed "+juce::String(processor.learnedOutput.load(),1)+" LUFS";
            if(std::abs(processor.learnedInput.load()-processor.learnedOutput.load()-db)>.1f) text+=" | Gain capped at 24 dB";
            else if(abButton.getToggleState()) text+=" | Listening to original";
            else if(!enableButton.getToggleState()) text+=" | Compensation disabled";
            else text+=" | Fixed gain applied";
        } else text="Play a representative section, then Learn Match.";
        matchStatus.setText(text,juce::dontSendNotification);
    }
    void timerCallback() override {
        refreshMatch();
        const auto sequence=processor.meterSequence.load(std::memory_order_relaxed);
        idleTicks=sequence==lastSequence?juce::jmin(idleTicks+1,16):0;
        lastSequence=sequence;
        // A host may stop audio callbacks when transport stops; do not freeze a needle.
        const auto l=idleTicks>15?0.f:processor.reductionL.load(std::memory_order_relaxed);
        const auto r=idleTicks>15?0.f:processor.reductionR.load(std::memory_order_relaxed);
        const float decay=std::exp(-1.f/(30.f*.28f));
        shownL=l>shownL?l:l+decay*(shownL-l);
        shownR=r>shownR?r:r+decay*(shownR-r);
        leftMeter.setReduction(shownL); rightMeter.setReduction(shownR);
    }
    juce::TextButton learnButton{"Learn Match"},resetButton{"Reset Match"},enableButton{"Match ON"},abButton{"B: Processed"};
    juce::Label readout,matchStatus;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment,abAttachment;
    ReductionMeter leftMeter{"LEFT"}, rightMeter{"RIGHT"};
    float shownL=0,shownR=0;
    uint32_t lastSequence=0;
    int idleTicks=0;
    ButterProcessor& processor;
    style::Look look;
    std::array<juce::Slider,3> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,3> attachments;
    juce::TooltipWindow tooltip{this,600};
};
juce::AudioProcessorEditor* ButterProcessor::createEditor() { return new ButterEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ButterProcessor; }
