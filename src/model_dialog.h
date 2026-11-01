#pragma once
#include "caption_models.h"

struct CaptionSelection {
    std::string recognition="zipformer-zh",translation,target="zh";
};
void ShowCaptionModelDialog(HWND owner,CaptionModelStore& store,CaptionSelection& selection,
    const std::function<void(const CaptionSelection&)>& apply,
    const std::function<bool(const std::string&)>& releaseForRemoval);
