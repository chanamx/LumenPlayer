#pragma once
#include <cstdint>
#include <cstddef>

// C ABI mirrored from the MIT-licensed moonshine-voice 0.1.5 Windows package.
// Keep this layout pinned to the verified DLL, rather than a moving SDK header.
struct MoonOption { const char* name; const char* value; };
struct MoonLine {
    const char* text;
    const float* audio_data;
    size_t audio_data_count;
    float start_time,duration;
    uint64_t id;
    int8_t is_complete,is_updated,is_new,has_text_changed,have_speakers_changed;
    const void* speaker_spans;
    uint64_t speaker_span_count;
    uint32_t last_transcription_latency_ms;
    const void* words;
    uint64_t word_count;
};
struct MoonTranscript { MoonLine* lines; uint64_t line_count; };
static_assert(sizeof(MoonLine)==88 && sizeof(MoonTranscript)==16,"Pinned Moonshine x64 ABI mismatch");
