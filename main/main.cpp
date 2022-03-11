
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "ESP_log.h"
#include "hx711.h"
#include <list>
#include <string.h>
#include "lib/tft/tft.h"
#include "filter.h"
#include "settings.h"
#include "lib/tcpip/tcpsocket.h"
#include "lib/misc/datetime.h"
#include "lib/rtos/queue.h"
#include "esp_timer.h"
#include "sample.h"
#include "sendsamples.h"

#define SSID "vanBassum"
#define PSWD "pedaalemmerzak"

HX711 hx711(GPIO_NUM_21, GPIO_NUM_19);
Filter<uint32_t> scaleFilter(64);
TFT tft = TFT::Get_ILI9341();
Font* font = 0;



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


void TakeSample(Sample* sample, int samples)
{
	uint64_t bigMeas = 0;
	for (int i = 0; i < samples; i++)
		bigMeas += hx711.Read();
	sample->Set(bigMeas / samples);
	sample->Samples = samples;
}

void DisplaySample(Sample s, Color c)
{
	char buf[32];
	snprintf(buf, 32, "%d    ", s.Raw);
	tft.DrawString(font, 0, 64, buf, c);
	snprintf(buf, 32, "%f    ", s.Weight);
	tft.DrawString(font, 0, 64 + 32, buf, c);
}

enum class State : uint8_t
{
	Standby,
	WaitForstable,
	CollectSample,
	Cooldown,
};

void app_main(void)
{
	nvs_flash_init();
	StartWIFI();
	tft.Init();
	font = new Font(ILGH32XB);
	tft.FillScreen(Color(0, 0, 0));
	tft.SetFontFill(Color(0, 0, 0));
	hx711.SetGain(2);
	InitSendSamples();

	Sample sample;
	State prvState = State::Standby;
	State actState = State::Standby;
	State nxtState = State::Standby;
	bool onEntry = true;
	while (true)
	{

		switch (actState)
		{
		case State::Standby:
			if (onEntry)
				tft.BacklightOff();
			tft.DrawString(font, 0, 128, "Standby        ", Color(255, 0, 0));
			DisplaySample(sample, Color(255, 0, 0));
			TakeSample(&sample, 5);
			if (sample.Weight > 2)
				nxtState = State::WaitForstable;
			else
				vTaskDelay(100/portTICK_PERIOD_MS);
			break;

		case State::WaitForstable:
			if (onEntry)
				tft.BacklightOn();
			tft.DrawString(font, 0, 128, "WaitForstable  ", Color(255, 0, 0));
			TakeSample(&sample, 5);
			DisplaySample(sample, Color(255, 0, 0));
			if (sample.Weight > 70)
				nxtState = State::CollectSample;
			else if (sample.Weight < 2)
				nxtState = State::Standby;
			break;

		case State::CollectSample:
			tft.DrawString(font, 0, 128, "CollectSample  ", Color(255, 0, 0));
			TakeSample(&sample, 20);
			DisplaySample(sample, Color(0, 255, 0));
			EnqueueSample(sample);
			nxtState = State::Cooldown;
			break;

		case State::Cooldown:
			tft.DrawString(font, 0, 128, "Cooldown       ", Color(255, 0, 0));
			TakeSample(&sample, 5);
			if (sample.Weight < 2)
				nxtState = State::Standby;
			else
				vTaskDelay(100/portTICK_PERIOD_MS);
			break;
		}

		onEntry = false;
		if (actState != nxtState)
		{
			prvState = actState;
			actState = nxtState;
			onEntry = true;
		}
	}
}

