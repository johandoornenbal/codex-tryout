#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
int main() {
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    if(p->getParameters().size()!=3 || !p->hasEditor()) return 1;
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
    if(editor->getWidth()!=840 || editor->getHeight()!=660) return 5;
    int meters=0;
    for(auto* child:editor->getChildren()) {
        if(child->getTitle().startsWith("Gain reduction ")) {
            ++meters;
            if(!editor->getLocalBounds().contains(child->getBounds())) return 6;
        }
    }
    if(meters!=2) return 7;
    editor.reset(); p->releaseResources();
    std::cout<<"Parameter state round-trip, stereo processing, and editor render passed\n";
}
