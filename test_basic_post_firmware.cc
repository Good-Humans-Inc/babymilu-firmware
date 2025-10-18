// Simple test function to send "hello world" message to SCF
bool AnimationUpdater::TestBasicPostToScf() {
    ESP_LOGI(TAG, "Testing basic POST request to SCF with hello world message");
    
    const std::string scf_url = "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/";
    
    try {
        // Create HTTP client
        auto& board = Board::GetInstance();
        auto http = std::unique_ptr<Http>(board.CreateHttp());
        
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client for basic POST test");
            return false;
        }
        
        // Prepare minimal JSON payload
        std::string json_body = "{\"message\":\"hello world\",\"test\":true}";
        
        ESP_LOGI(TAG, "Sending minimal JSON payload: %s", json_body.c_str());
        ESP_LOGI(TAG, "Payload size: %lu bytes", (unsigned long)json_body.size());
        
        // Set HTTP headers
        http->SetHeader("Content-Type", "application/json");
        http->SetHeader("Content-Length", std::to_string(json_body.size()));
        http->SetHeader("User-Agent", "Xiaozhi-Test/1.0");
        http->SetTimeout(15000); // 15 second timeout
        
        ESP_LOGI(TAG, "Opening POST connection to SCF...");
        
        // Open POST connection
        if (!http->Open("POST", scf_url)) {
            ESP_LOGE(TAG, "Failed to open HTTPS connection to SCF URL");
            return false;
        }
        
        ESP_LOGI(TAG, "Sending JSON data (%lu bytes)...", (unsigned long)json_body.size());
        
        // Send the JSON data
        size_t bytes_sent = http->Write(json_body.c_str(), json_body.size());
        ESP_LOGI(TAG, "HTTP Write completed - sent %lu bytes", (unsigned long)bytes_sent);
        
        // Check if we sent the data
        if (bytes_sent < json_body.size() * 0.9) {  // Allow 10% tolerance
            ESP_LOGE(TAG, "Failed to send sufficient data (sent: %lu, expected: %lu)", 
                     (unsigned long)bytes_sent, (unsigned long)json_body.size());
            http->Close();
            return false;
        }
        
        // Check response
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "SCF response status: %d", status_code);
        
        if (status_code == -1) {
            ESP_LOGW(TAG, "SCF request failed - connection timeout or network error");
            http->Close();
            return false;
        } else if (status_code != 200) {
            ESP_LOGW(TAG, "SCF returned non-200 status: %d", status_code);
            http->Close();
            return false;
        }
        
        // Read response
        std::string response = http->ReadAll();
        ESP_LOGI(TAG, "SCF response length: %lu bytes", (unsigned long)response.length());
        
        if (response.empty()) {
            ESP_LOGW(TAG, "SCF returned empty response");
            http->Close();
            return false;
        }
        
        ESP_LOGI(TAG, "SCF response: %s", response.c_str());
        
        http->Close();
        
        ESP_LOGI(TAG, "✅ Basic POST test successful!");
        return true;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in TestBasicPostToScf: %s", e.what());
        return false;
    }
}

