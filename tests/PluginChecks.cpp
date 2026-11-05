#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include "../Source/ButterEngine.h"
int main() {
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    if(p->getParameters().size()!=5 || !p->hasEditor()) return 1;
    p->setRateAndBufferSizeDetails(48000,512); p->prepareToPlay(48000,512);
    p->getParameters()[0]->setValueNotifyingHost(.7f);
    juce::MemoryBlock state; p->getStateInformation(state);
    p->getParameters()[0]->setValueNotifyingHost(.1f);
    p->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    if(std::abs(p->getParameters()[0]->getValue()-.7f)>1e-6f) return 2;
    juce::AudioBuffer<float> audio(2,512);
    for(int ch=0;ch<2;++ch) for(int i=0;i<512;++i) audio.setSample(ch,i,.4f*std::sin(i*.05f));
    juce::MidiBuffer midi; p->processBlock(audio,midi);
    if(!std::isfinite(audio.getMagnitude(0,512)) || audio.getMagnitude(0,512)<.01f) return 3;
    // With no learned gain, the wrapper must be bit-identical to the original
    // engine, including when the host block exceeds the internal scratch size.
    for(bool useDouble : {false,true}) {
        p->setProcessingPrecision(useDouble?juce::AudioProcessor::doublePrecision:juce::AudioProcessor::singlePrecision);
        p->prepareToPlay(48000,1025);
        ButterEngine direct; direct.reset(48000); direct.parameters(.7f,.5f,1);
        if(useDouble) {
            juce::AudioBuffer<double> input(2,1025),expected(2,1025);
            for(int ch=0;ch<2;++ch) for(int i=0;i<1025;++i) input.setSample(ch,i,.3*std::sin(i*.07+ch));
            expected.makeCopyOf(input); double* data[]={expected.getWritePointer(0),expected.getWritePointer(1)};
            direct.processDoubleReplacing(data,data,1025); p->processBlock(input,midi);
            for(int ch=0;ch<2;++ch) for(int i=0;i<1025;++i) if(input.getSample(ch,i)!=expected.getSample(ch,i)) return 20;
        } else {
            juce::AudioBuffer<float> input(2,1025),expected(2,1025);
            for(int ch=0;ch<2;++ch) for(int i=0;i<1025;++i) input.setSample(ch,i,.3f*std::sin(i*.07f+ch));
            expected.makeCopyOf(input); float* data[]={expected.getWritePointer(0),expected.getWritePointer(1)};
            direct.processReplacing(data,data,1025); p->processBlock(input,midi);
            for(int ch=0;ch<2;++ch) for(int i=0;i<1025;++i) if(input.getSample(ch,i)!=expected.getSample(ch,i)) return 21;
        }
    }
    p->setProcessingPrecision(juce::AudioProcessor::singlePrecision); p->prepareToPlay(48000,512);
    // Exercise both meters with a sustained stereo signal before rendering.
    for(int block=0;block<280;++block) {
        for(int i=0;i<512;++i) {
            const auto phase=static_cast<float>(block*512+i);
            audio.setSample(0,i,.9f*std::sin(phase*.05f));
            audio.setSample(1,i,.22f*std::sin(phase*.031f));
        }
        p->processBlock(audio,midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    // A known 2x output gain must learn -6.0206 dB without modifying the DSP.
    p->getParameters()[0]->setValueNotifyingHost(0.f);
    p->getParameters()[1]->setValueNotifyingHost(1.f);
    p->getParameters()[2]->setValueNotifyingHost(1.f);
    p->prepareToPlay(48000,512);
    auto* learn=dynamic_cast<juce::Button*>(editor->findChildWithID("learnMatch"));
    auto* reset=dynamic_cast<juce::Button*>(editor->findChildWithID("resetMatch"));
    if(!learn || !reset) return 10;
    learn->onClick();
    auto feed=[&](int blocks) {
        for(int block=0;block<blocks;++block) {
            for(int i=0;i<512;++i) {
                auto value=.2f*std::sin(static_cast<float>(block*512+i)*.13f);
                audio.setSample(0,i,value); audio.setSample(1,i,value*.6f);
            }
            p->processBlock(audio,midi);
        }
    };
    feed(1550);
    for(int i=0;i<512;++i) {
        auto expected=.2f*std::sin(static_cast<float>(1549*512+i)*.13f);
        if(std::abs(audio.getSample(0,i)-expected)>2e-6f) return 18;
    }
    // Compensation is fixed after learning; further processing must retain the trim.
    feed(5);
    juce::MemoryBlock learned; p->getStateInformation(learned);
    auto xml=juce::AudioProcessor::getXmlFromBinary(learned.getData(),static_cast<int>(learned.getSize()));
    if(!xml || !xml->getBoolAttribute("hasMatch") || std::abs(xml->getDoubleAttribute("matchDb")+6.0206)>.03) return 11;
    // Learned adjustment is frozen and restores in a fresh instance.
    std::unique_ptr<juce::AudioProcessor> restored(createPluginFilter());
    restored->setStateInformation(learned.getData(),static_cast<int>(learned.getSize()));
    restored->prepareToPlay(48000,512);
    juce::MemoryBlock savedAgain; restored->getStateInformation(savedAgain);
    auto copy=juce::AudioProcessor::getXmlFromBinary(savedAgain.getData(),static_cast<int>(savedAgain.getSize()));
    if(!copy || std::abs(copy->getDoubleAttribute("matchDb")+6.0206)>.03) return 12;
    // Disabling matching restores the wet output while retaining the learned gain.
    p->getParameters()[3]->setValueNotifyingHost(0.f); feed(5);
    for(int i=0;i<512;++i) {
        auto expected=.4f*std::sin(static_cast<float>(4*512+i)*.13f);
        if(std::abs(audio.getSample(0,i)-expected)>2e-6f) return 22;
    }
    p->getParameters()[3]->setValueNotifyingHost(1.f); feed(5);
    // Relearning measures before the previous compensation, so it must learn -6 dB again.
    learn->onClick(); feed(1550);
    p->getStateInformation(savedAgain);
    copy=juce::AudioProcessor::getXmlFromBinary(savedAgain.getData(),static_cast<int>(savedAgain.getSize()));
    if(!copy || std::abs(copy->getDoubleAttribute("matchDb")+6.0206)>.03) return 23;
    // Recreate the editor to verify the persisted adjustment is visible immediately.
    editor.reset(p->createEditor());
    auto* display=dynamic_cast<juce::Label*>(editor->findChildWithID("matchReadout"));
    if(!display || !display->getText().contains("-6.0 dB")) return 13;
    // Screenshot the real learned state before clearing it.
    juce::Image image(juce::Image::ARGB,editor->getWidth(),editor->getHeight(),true);
    juce::Graphics g(image); editor->paintEntireComponent(g,true);
    juce::File target=juce::File::getCurrentWorkingDirectory().getChildFile("ui-preview.png");
    if(auto out=target.createOutputStream()) {
        out->setPosition(0); out->truncate();
        if(!juce::PNGImageFormat().writeImageToStream(image,*out)) return 8;
    }
    else return 4;
    auto saved=juce::ImageFileFormat::loadFrom(target);
    if(saved.getWidth()!=editor->getWidth() || saved.getHeight()!=editor->getHeight()) return 9;
    if(editor->getWidth()!=840 || editor->getHeight()!=810) return 5;
    int meters=0;
    for(auto* child:editor->getChildren()) {
        if(child->getTitle().startsWith("Gain reduction ")) {
            ++meters;
            if(!editor->getLocalBounds().contains(child->getBounds())) return 6;
        }
    }
    if(meters!=2) return 7;
    reset=dynamic_cast<juce::Button*>(editor->findChildWithID("resetMatch"));
    reset->onClick();
    auto* clearDisplay=dynamic_cast<juce::Label*>(editor->findChildWithID("matchReadout"));
    if(!clearDisplay || !clearDisplay->getText().contains("--")) return 19;
    feed(4);
    juce::MemoryBlock cleared; p->getStateInformation(cleared);
    auto resetXml=juce::AudioProcessor::getXmlFromBinary(cleared.getData(),static_cast<int>(cleared.getSize()));
    if(!resetXml || resetXml->getBoolAttribute("hasMatch") || resetXml->getDoubleAttribute("matchDb")!=0) return 14;
    if(p->getParameters()[0]->getValue()!=0 || p->getParameters()[1]->getValue()!=1 || p->getParameters()[2]->getValue()!=1) return 15;
    // Internal A/B returns the original samples after its short switching fade.
    p->getParameters()[4]->setValueNotifyingHost(1.f); feed(4);
    for(int i=0;i<512;++i) {
        auto expected=.2f*std::sin(static_cast<float>(3*512+i)*.13f);
        if(audio.getSample(0,i)!=expected || audio.getSample(1,i)!=expected*.6f) return 16;
    }
    // A legacy three-parameter session opens with no learned adjustment.
    xml->removeAttribute("matchDb"); xml->removeAttribute("hasMatch");
    for(int i=xml->getNumChildElements()-1;i>=0;--i) {
        auto* child=xml->getChildElement(i);
        if(child->getStringAttribute("id")=="original" || child->getStringAttribute("id")=="matchEnabled") xml->removeChildElement(child,true);
    }
    restored->getParameters()[4]->setValueNotifyingHost(1.f);
    juce::MemoryBlock legacy; juce::AudioProcessor::copyXmlToBinary(*xml,legacy);
    restored->setStateInformation(legacy.getData(),static_cast<int>(legacy.getSize()));
    restored->getStateInformation(savedAgain);
    copy=juce::AudioProcessor::getXmlFromBinary(savedAgain.getData(),static_cast<int>(savedAgain.getSize()));
    if(copy->getBoolAttribute("hasMatch") || restored->getParameters()[4]->getValue()>.5f) return 17;
    editor.reset(); p->releaseResources();
    std::cout<<"Parameter state round-trip, stereo processing, and editor render passed\n";
}
