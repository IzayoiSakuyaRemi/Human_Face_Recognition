/* Stub: ESP wake word — not used on S3 */
#pragma once
#include "../wake_word.h"
class EspWakeWord : public WakeWord {
public:
    bool Initialize(AudioCodec*, srmodel_list_t*) override { return true; }
    void Feed(const std::vector<int16_t>&) override {}
    void OnWakeWordDetected(std::function<void(const std::string&)> cb) override {}
    void Start() override {}
    void Stop() override {}
    size_t GetFeedSize() override { return 0; }
    void EncodeWakeWordData() override {}
    bool GetWakeWordOpus(std::vector<uint8_t>& opus) override { return false; }
    const std::string& GetLastDetectedWakeWord() const override { static std::string e; return e; }
};
