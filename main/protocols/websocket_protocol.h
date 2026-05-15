#ifndef _WEBSOCKET_PROTOCOL_H_
#define _WEBSOCKET_PROTOCOL_H_


#include "protocol.h"

#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#define WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

class WebsocketProtocol : public Protocol {
public:
    WebsocketProtocol();
    ~WebsocketProtocol();

    bool Start() override;
    bool SendAudio(const AudioStreamPacket& packet) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

private:
    EventGroupHandle_t event_group_handle_;
    WebSocket* websocket_ = nullptr;
    int version_ = 1;
    int frame_count_ = 0;  // Counter for Opus frames sent
    int audio_metadata_frame_count_ = 0;
    uint32_t audio_metadata_start_sequence_ = 0;
    uint32_t audio_metadata_start_timestamp_ms_ = 0;

    void ParseServerHello(const cJSON* root);
    bool SendText(const std::string& text) override;
    std::string GetHelloMessage();
    void MaybeSendAudioMetadata(const AudioStreamPacket& packet);
};

#endif
