
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "ESP_log.h"
#include <string.h>

#include "lib/rtos/stream.h"


#define SSID "vanBassum"
#define PSWD "pedaalemmerzak"

extern "C" {
	void app_main();
}

esp_err_t event_handler(void* ctx, system_event_t* event)
{
	return ESP_OK;
}

void StartWIFI()
{
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	wifi_config_t sta_config = { };
	memcpy(sta_config.sta.ssid, SSID, strlen(SSID));
	memcpy(sta_config.sta.password, PSWD, strlen(PSWD));
	sta_config.sta.bssid_set = false;
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_ERROR_CHECK(esp_wifi_connect());

	setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
	tzset();
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, "pool.ntp.org");
	sntp_init();
}


void app_main(void)
{
	nvs_flash_init();
	StartWIFI();
	
}


namespace Framing
{
	class COBS
	{
		class Decoder
		{
			FreeRTOS::MessageBuffer& OutputStream;
			FreeRTOS::StreamBuffer& InputBuffer; 
			bool memOverflow = false;
			uint8_t nextZero = 0;
			uint8_t* buffer = NULL;
			size_t bufferPtr = 0;
			size_t bufferSize = 0;
			
		public:
			void Init(FreeRTOS::MessageBuffer& outputStream, FreeRTOS::StreamBuffer& inputBuffer, size_t inputBufferSize)
			{
				if (inputBufferSize < 255)
					bufferSize = 255;
				else
					bufferSize = inputBufferSize;
				
				OutputStream = outputStream;
				InputBuffer = inputBuffer;
				buffer = (uint8_t *)malloc(bufferSize);
			}
			
			void Work(TickType_t wait)
			{
				//Sanity check!
				if (bufferSize >= 255 && buffer != NULL)
				{
					if (nextZero == 0) //Start of frame, First byte shows where the next zero is.
					{
						memOverflow = false;
						bufferPtr = 0;
						nextZero = 1;
					}
					if (bufferPtr + nextZero > bufferSize)	
					{
						ESP_LOGE("COBS_Decode", "Message larger than buffer");
						memOverflow = true;
						bufferPtr = 0;				//We still need to read the rest of the data, untill a 0 has been discovered. Since the buffer is at least 255, this will ensure enough bytes can be read.
					}
					InputBuffer.SetTriggerLevel(nextZero);
					size_t len = InputBuffer.Receive(&buffer[bufferPtr], nextZero, wait);
					bufferPtr += len;
					nextZero -= len;
					if (nextZero == 0)
					{
						nextZero = buffer[bufferPtr - 1];
						if (nextZero == 0)
						{
							//Full messge received
							if (!memOverflow)
							{
								if (OutputStream.Send(buffer, bufferPtr + 1, wait) != bufferPtr + 1)
									ESP_LOGE("COBS_Decode", "Not enough memory in output stream");
							}
							else
								ESP_LOGE("COBS_Decode", "End of message found, but buffer overflowed. Message discarded");
						}
						else
							bufferPtr--; //Overwrite the nextZeroPointer with more data.
					}
				}
				else
					ESP_LOGE("COBS_Decode", "Not initialized");
			}
		};
	};
}







