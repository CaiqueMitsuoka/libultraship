#include "ship/audio/AudioPlayer.h"
#include "spdlog/spdlog.h"

namespace Ship {

AudioPlayer::~AudioPlayer() {
    SPDLOG_TRACE("destruct audio player");
}

bool AudioPlayer::Init() {
    // Initialize sound matrix decoder if matrix surround mode is enabled
    if (mAudioSettings.ChannelSetting == AudioChannelsSetting::audioMatrix51) {
        SPDLOG_INFO("Initializing sound matrix decoder for surround");
        mSoundMatrixDecoder = std::make_unique<SoundMatrixDecoder>(mAudioSettings.SampleRate);
    }
    mInitialized = DoInit();
    return IsInitialized();
}

bool AudioPlayer::IsInitialized() {
    return mInitialized;
}

int32_t AudioPlayer::GetSampleRate() const {
    return mAudioSettings.SampleRate;
}

int32_t AudioPlayer::GetSampleLength() const {
    return mAudioSettings.SampleLength;
}

int32_t AudioPlayer::GetDesiredBuffered() const {
    return mAudioSettings.DesiredBuffered;
}

AudioChannelsSetting AudioPlayer::GetAudioChannels() const {
    return mAudioSettings.ChannelSetting;
}

void AudioPlayer::SetSampleRate(int32_t rate) {
    mAudioSettings.SampleRate = rate;
}

void AudioPlayer::SetSampleLength(int32_t length) {
    mAudioSettings.SampleLength = length;
}

void AudioPlayer::SetDesiredBuffered(int32_t size) {
    mAudioSettings.DesiredBuffered = size;
}

bool AudioPlayer::SetAudioChannels(AudioChannelsSetting channels) {
    if (mAudioSettings.ChannelSetting == channels) {
        return true; // No change needed
    }

    SPDLOG_INFO("Changing audio channels from {} to {}", AudioChannelsSettingName(mAudioSettings.ChannelSetting),
                AudioChannelsSettingName(channels));

    // Close current audio device
    DoClose();

    // Update channel setting
    mAudioSettings.ChannelSetting = channels;

    // Setup or teardown sound matrix decoder
    if (channels == AudioChannelsSetting::audioMatrix51) {
        if (!mSoundMatrixDecoder) {
            mSoundMatrixDecoder = std::make_unique<SoundMatrixDecoder>(mAudioSettings.SampleRate);
        }
    } else {
        // When switching away from matrix mode, release the decoder
        mSoundMatrixDecoder.reset();
    }

    return DoInit();
}

int32_t AudioPlayer::GetNumOutputChannels() const {
    switch (mAudioSettings.ChannelSetting) {
        case AudioChannelsSetting::audioMatrix51:
        case AudioChannelsSetting::audioRaw51:
            return 6;
        case AudioChannelsSetting::audioStereo:
        default:
            return 2;
    }
}

// Defined in src/port/AudioStreamer.cpp (main app target, not this submodule
// - forward-declared here instead of #included since libultraship's include
// path doesn't reach src/port, same as FrameStreamer/Interpreter::EndFrame).
// See docs/membrane-integration.md.
extern "C" void AudioStreamer_CaptureAudio(const uint8_t* buf, size_t len, int sampleRate, int channels);

void AudioPlayer::Play(const uint8_t* buf, size_t len) {
    if (mAudioSettings.ChannelSetting != AudioChannelsSetting::audioMatrix51) {
        // Stereo or Raw 5.1 passthrough - buf/len already match
        // GetNumOutputChannels() in this branch, so it's safe to stream as-is.
        // (Matrix-51 mode is skipped below: buf here is still pre-decode
        // stereo while GetNumOutputChannels() would report 6, which would
        // mislabel the capture.)
        AudioStreamer_CaptureAudio(buf, len, GetSampleRate(), GetNumOutputChannels());
        // DoPlay(buf, len) deliberately not called: local audio output is off
        // for the streaming setup - only AudioStreamer's consumer should hear
        // this. See docs/membrane-integration.md.
        return;
    }

    if (!mSoundMatrixDecoder) {
        SPDLOG_ERROR("AudioPlayer: Matrix 5.1 mode enabled but SoundMatrixDecoder is not initialized");
        return;
    }

    // Decode stereo to surround using sound matrix decoder
    const auto [surroundOut, surroundLen] = mSoundMatrixDecoder->Process(buf, len);

    // Local audio output is off for the streaming setup (see the non-matrix
    // branch above) - matrix-51 mode isn't captured (docs/membrane-integration.md
    // notes the gap), so this path is silent either way rather than
    // half-working (heard locally, not streamed).
    (void)surroundOut;
    (void)surroundLen;
}
} // namespace Ship
