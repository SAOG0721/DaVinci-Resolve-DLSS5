// SPDX-License-Identifier: MIT

#pragma once

#include "ofxsImageEffect.h"

class ResolveDlss5PluginFactory final : public OFX::PluginFactoryHelper<ResolveDlss5PluginFactory> {
   public:
    ResolveDlss5PluginFactory();

    void load() override;
    void unload() override;
    OfxPluginEntryPoint* getMainEntry() override;

    static OfxStatus diagnosticMainEntry(const char* action, const void* handle,
                                         OfxPropertySetHandle in, OfxPropertySetHandle out);
    void describe(OFX::ImageEffectDescriptor& descriptor) override;
    void describeInContext(OFX::ImageEffectDescriptor& descriptor,
                           OFX::ContextEnum context) override;
    OFX::ImageEffect* createInstance(OfxImageEffectHandle handle,
                                     OFX::ContextEnum context) override;
};
