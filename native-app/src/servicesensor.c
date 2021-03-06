#include <tizen.h>
#include <service_app.h>
#include <sensor.h>
#include "servicesensor.h"
#include "message_port.h"
#include "stdio.h"
#include "time.h"
#include "glib.h"
#include "stdlib.h"
#include "storage.h"
#include <audio_io.h>
#include <sound_manager.h>
#include "wifi-manager.h"
#include "wifi.h"
#include "bluetooth.h"
#include "Ecore.h"


typedef struct {
	sensor_listener_h listener[50];
	sensor_h sensor[50];
	app_control_h reply;
	app_control_h app_control_g;

} appdata_s;

bool running = false;


/******
 * Recorder data
 */


audio_in_h input;
FILE *fp_audio;

typedef struct wavfile_header_s
{
    char    ChunkID[4];     /*  4   */
    int32_t ChunkSize;      /*  4   */
    char    Format[4];      /*  4   */

    char    Subchunk1ID[4]; /*  4   */
    int32_t Subchunk1Size;  /*  4   */
    int16_t AudioFormat;    /*  2   */
    int16_t NumChannels;    /*  2   */
    int32_t SampleRate;     /*  4   */
    int32_t ByteRate;       /*  4   */
    int16_t BlockAlign;     /*  2   */
    int16_t BitsPerSample;  /*  2   */
    char    Subchunk2ID[4]; /*  4   */
    int32_t Subchunk2Size;	/*  4   */
} wavfile_header_t;


#define SUBCHUNK1SIZE   (16)
#define AUDIO_FORMAT    (1) /*For PCM*/
#define NUM_CHANNELS    (1) // only mono
#define SAMPLE_RATE 	(16000)

#define BITS_PER_SAMPLE (16)

#define BYTE_RATE       (SAMPLE_RATE * NUM_CHANNELS * BITS_PER_SAMPLE/8)
#define BLOCK_ALIGN     (NUM_CHANNELS * BITS_PER_SAMPLE/8)






/**
 * Sensor Data
 *
 *
 */
int listenerSize= 0;

FILE *fp;
FILE *fpaccel;
FILE *fpgyr;
FILE *fplight;
FILE *fppress;
FILE *fpble;
FILE *fpwifi;


/*
 * Returns corresponding File pointer to write sensor data to
 * If an additional sensor is planed to be added you might want to add a new *FILE here too @see open_all_files() and @see close_all_files()
 */
FILE *get_file_by_type(sensor_type_e type){
	switch(type){
		case SENSOR_ACCELEROMETER : return fpaccel;
		case SENSOR_GRAVITY: 		return fp;
		case SENSOR_LINEAR_ACCELERATION:return fp;
		case SENSOR_MAGNETIC:		return fp;
		case SENSOR_ROTATION_VECTOR:return fp;
		case SENSOR_ORIENTATION:	return fp;
		case SENSOR_GYROSCOPE:		return fpgyr;
		case SENSOR_LIGHT:			return fplight;
		case SENSOR_PROXIMITY:		return fp;
		case SENSOR_PRESSURE:		return fppress;
		case SENSOR_ULTRAVIOLET:	return fp;
		case SENSOR_TEMPERATURE:	return fp;
		case SENSOR_HUMIDITY:       return fp;
		}
	return fp;
}


/*
 * Returns String for given Sensor Type
 */
void getNamebyType(sensor_type_e type, char** name){
	switch(type){
	case SENSOR_ACCELEROMETER : name[0] = "ACCELEROMETER"; break;
	case SENSOR_GRAVITY: 		name[0] = "GRAVITY";break;
	case SENSOR_LINEAR_ACCELERATION:name[0] = "LINEAR_ACCELERATION";break;
	case SENSOR_MAGNETIC:		name[0] = "MAGNETIC";break;
	case SENSOR_ROTATION_VECTOR:name[0] = "ROTATION_VECTOR";break;
	case SENSOR_ORIENTATION:	name[0] = "ORIENTATION";break;
	case SENSOR_GYROSCOPE:		name[0] = "GYROSCOPE";break;
	case SENSOR_LIGHT:			name[0] = "LIGHT";break;
	case SENSOR_PROXIMITY:		name[0] = "PROXIMITY";break;
	case SENSOR_PRESSURE:		name[0] = "PRESSURE";break;
	case SENSOR_ULTRAVIOLET:	name[0] = "ULTRAVIOLET";break;
	case SENSOR_TEMPERATURE:	name[0] = "TEMPERATURE";break;
	case SENSOR_HUMIDITY:       name[0] = "HUMIDITY";break;
	}
}

int i = 0;

/**
 * BLuetooth and wifi
 */



wifi_manager_h *wifi_manager;
GList *devices_list_ble;
GList *devices_list_wifi;


typedef struct {
	int rssid;
	char *bssid;
} wifi_s ;


/*
 * Add found Wifi devices to to list same as BLuetooth with MAC address and signal strength
 */
bool __wifi_manager_found_ap_cb(wifi_ap_h ap, void *user_data) {
    int rssi;
    char *bssid;
    char* ap_id = NULL;
    char name[50];

    wifi_ap_get_rssi(ap, &rssi);
	wifi_ap_get_bssid(ap, &bssid);
	sprintf(name, "%s",bssid);
    wifi_s *new_wifi_ap = malloc(sizeof(wifi_s));
    new_wifi_ap->bssid = strdup(bssid);
    new_wifi_ap->rssid = rssi;
    devices_list_wifi = g_list_append(devices_list_wifi, (gpointer)new_wifi_ap);
    return true;
}


/*
 * Callback for found Bluetooth LE devices which are written to a list which is printed every 10 seconds and then renewed
 */
void _bluetooth_le_cb (int result,bt_adapter_le_device_scan_result_info_s *info, void *user_data){
	//simply writes MAC address and signal Strength to list and then to file
	wifi_s *new_ble_ap = malloc(sizeof(wifi_s));
	new_ble_ap->bssid = strdup(info->remote_address);//address
	new_ble_ap->rssid = info->rssi;//strength
	if (info != NULL) { //list ti append data to
		devices_list_ble = g_list_append(devices_list_ble, (gpointer)new_ble_ap);
	}

}


/*
 * Callback for found Bluetooth devices which are written to a list which is printed every 10 seconds and then renewed
 * Not used since we changed to Bluetooth LE
 */
void adapter_device_discovery_state_changed_cb(int result, bt_adapter_device_discovery_state_e discovery_state,
											bt_adapter_device_discovery_info_s *discovery_info, void* user_data){

	if (result == BT_ERROR_NONE) {
		switch (discovery_state) {
		 	 case BT_ADAPTER_DEVICE_DISCOVERY_STARTED:
				 sendMessage("BLE started dicovery");
				 break;
			 case BT_ADAPTER_DEVICE_DISCOVERY_FINISHED:
				 sendMessage("finished discovery");
				 break;
			 case BT_ADAPTER_DEVICE_DISCOVERY_FOUND: {
				 wifi_s *new_ble_ap = malloc(sizeof(wifi_s));
				 new_ble_ap->bssid = strdup(discovery_info->remote_address);
				 new_ble_ap->rssid = discovery_info->rssi;
				 if (discovery_info != NULL) {
					 devices_list_ble = g_list_append(devices_list_ble, (gpointer)new_ble_ap);
				 }
				 sendMessage("New Ble device found");
			 }
	 }
 }else {
	 sendMessage("Error");
	 bt_adapter_start_device_discovery();
 }
}


/*
 * Callback for Sensors
 */
void sensor_cb(sensor_h sensor, sensor_event_s *event, void *user_data){
	time_t raw_time;
	struct timespec time_spec;
	clock_gettime(CLOCK_REALTIME, &time_spec);
	unsigned long long ms = time_spec.tv_nsec / 1000000LL;
	time(&raw_time);
	struct tm* l_time = localtime(&raw_time);
	char fBuffer[100] = "";
	char timeStamp[100] = "";
	char out [1000] = "";
	sensor_type_e type;
	sensor_get_type(sensor, &type);
	//create string to write to file
	if (type == SENSOR_PRESSURE) {
		//just take first val since somehow we get 4 values by the api
		sprintf(fBuffer, "%f ",event->values[0]);
		strcat(out, fBuffer);
	} else {
		for (int i = 0; i < event->value_count; i++){
			//fArray[i] = &fBuffer[i];
			sprintf(fBuffer, "%f ",event->values[i]);
			strcat(out, fBuffer);
		}
	}
	//append strings
	sprintf(timeStamp, "%d-%02d-%02dT%02d:%02d:%02d.%03llu",l_time->tm_year+1900,l_time->tm_mon+1,l_time->tm_mday,l_time->tm_hour,l_time->tm_min,l_time->tm_sec,ms);
	strcat(out, timeStamp);
	strcat(out, "\r\n");
	//write to file
	fputs(out, get_file_by_type(type));
}


/*
 * Stops audio and closes File
 * Inconsistent with close_all_files
 */
void stop_audio(){
	audio_in_unprepare(input);
	audio_in_unset_stream_cb(input);
	write_PCM16_stereo_header(fp_audio);
	audio_in_destroy(input);
	fclose(fp_audio);

}


/*
 * Starts scanning for Bluetooth LE devices
 */
void start_ble_scanner (){
	fpble = fopen(get_write_filepath("ble.txt"), "w");
	devices_list_ble = NULL;
	bt_initialize();
	bt_adapter_le_start_scan(_bluetooth_le_cb, NULL);


}

void __scan_request_cb(wifi_error_e error_code, void *user_data) {
    wifi_foreach_found_aps(__wifi_manager_found_ap_cb, NULL);
}


/*
 * simply starts scanning for wifi devices
 */
void start_wifi_scanner(){
	fpwifi = fopen(get_write_filepath("wifi.txt"), "w");
	wifi_initialize();
	wifi_scan(__scan_request_cb, NULL);
}


/**
 * Writes PCM16 header to existing audio File and overwrites the first 44 bytes so concider calling
 * write_fake_header first
 */
int write_PCM16_stereo_header(FILE*   file_p)
{
    int ret;

    wavfile_header_t wav_header;
    int32_t subchunk2_size;
    int32_t chunk_size;

    size_t write_count;

    fseek(file_p, 0, SEEK_END);


    subchunk2_size  = ftell(file_p) - 44;
    chunk_size      = subchunk2_size + 36;

    wav_header.ChunkID[0] = 'R';
    wav_header.ChunkID[1] = 'I';
    wav_header.ChunkID[2] = 'F';
    wav_header.ChunkID[3] = 'F';

    wav_header.ChunkSize = chunk_size;

    wav_header.Format[0] = 'W';
    wav_header.Format[1] = 'A';
    wav_header.Format[2] = 'V';
    wav_header.Format[3] = 'E';

    wav_header.Subchunk1ID[0] = 'f';
    wav_header.Subchunk1ID[1] = 'm';
    wav_header.Subchunk1ID[2] = 't';
    wav_header.Subchunk1ID[3] = ' ';

    wav_header.Subchunk1Size = SUBCHUNK1SIZE;
    wav_header.AudioFormat = AUDIO_FORMAT;
    wav_header.NumChannels = NUM_CHANNELS;
    wav_header.SampleRate = SAMPLE_RATE;
    wav_header.ByteRate = BYTE_RATE;
    wav_header.BlockAlign = BLOCK_ALIGN;
    wav_header.BitsPerSample = BITS_PER_SAMPLE;

    wav_header.Subchunk2ID[0] = 'd';
    wav_header.Subchunk2ID[1] = 'a';
    wav_header.Subchunk2ID[2] = 't';
    wav_header.Subchunk2ID[3] = 'a';
    wav_header.Subchunk2Size = subchunk2_size;
    fseek(file_p, 0, SEEK_SET);

    write_count = fwrite( &wav_header,
                            sizeof(wavfile_header_t), 1,
                            file_p);

    ret = (1 != write_count)? -1 : 0;

    return ret;
}


/*
 * on receive Messages from host app actually ther is just one command which should be stop this could be extended if needed by comparing the strings and commands
 */
void
message_port_cb(int local_port_id, const char *remote_app_id, const char *remote_port,
                bool trusted_remote_port, bundle *message, void *user_data)
{

	char *m;

	bundle_get_str(message, "key", &m);

	if (strcmp(m, "status")){
		//char *ret;
		//sprintf(ret, "%s",running ? "true" : "false");
		//sendMessage(ret);
	}
	stop_audio();
	stopListener(user_data);
	service_app_exit();

}


bool is_supported(sensor_type_e type){
	bool b;
	sensor_is_supported(type, &b);
	return b;
}

/*
 * Add a given listener to its sensor with the interval in ms
 */
void add_listener(int index, sensor_type_e type, sensor_event_cb cb_func, void *data, int interval){
	appdata_s *ad = (appdata_s *) data;
	int min_interval;
	//char sec[50];
	sensor_get_default_sensor(type,&ad->sensor[listenerSize]);
	sensor_create_listener(ad->sensor[listenerSize], &ad->listener[listenerSize]);
	sensor_get_min_interval(ad->sensor[listenerSize], &min_interval);
	sensor_listener_set_event_cb(ad->listener[listenerSize], interval, cb_func, NULL);
	sensor_listener_set_option(ad->listener[listenerSize], SENSOR_OPTION_ALWAYS_ON);
	sensor_listener_start(ad->listener[listenerSize]);
	listenerSize++;

}



bool service_app_create(void *data)
{
    // Todo: add your code here.
	//Actually do nothing when started by another app it will be called in
	//dlog_print(DLOG_DEBUG, "MYTAG", "Create Called");
	//startListener(data);
    return true;
}

/*
 * Send Messages to host app
 * easiest way to debugg the app
 */
void sendMessage(const char *str){
	bundle *b = bundle_create();
	bundle_add_str(b, "Message", str);
	message_port_send_message("UM2FcDkLYZ.HelloAccessoryProvider", "MY_PORT", b);
	bundle_free(b);
}


/*
 * Closes all Sensor Files
 */
void close_all_files(){
	fclose(fpgyr);
	fclose(fpaccel);
	fclose(fplight);
	fclose(fppress);
}

/*
 * App Terminates and Closes all Sensor files
 */
void service_app_terminate(void *data)
{
	//appdata_s *ad = (appdata_s *) data;
	bundle *b = bundle_create();

	bundle_add_str(b, "command", "service_app_terminate");
	message_port_send_message("UM2FcDkLYZ.HelloAccessoryProvider", "MY_PORT", b);
	bundle_free(b);
	close_all_files();

    return;
}

/*
 * opens all Sensor Files if you add another sensor below you also want to add the files here and add them to close_all_files()
 */
void open_all_files(){
	fpaccel = fopen(get_write_filepath("accData.txt"), "w");
	fpgyr =fopen(get_write_filepath("gyrData.txt"), "w");
	fplight=fopen(get_write_filepath("luxData.txt"), "w");
	fppress= fopen(get_write_filepath("barData.txt"), "w");
}

/*
 * Check if sensor is avaialbe and add them to start list
 */
void startListener(void *data){
	appdata_s *ad = (appdata_s *) data;
	open_all_files();
	if (is_supported(SENSOR_ACCELEROMETER)) {
			sendMessage("Accel Supported");
			add_listener(0,SENSOR_ACCELEROMETER, sensor_cb,data,20); // 20 ms = 50 hz
		}

		if (is_supported(SENSOR_LINEAR_ACCELERATION)){
			sendMessage("Lin Accel Supported");
			//add_listener(1,SENSOR_LINEAR_ACCELERATION, sensor_cb,data);
		}
		if (is_supported(SENSOR_GYROSCOPE)){
			sendMessage("Gyro Supported");
			add_listener(2,SENSOR_GYROSCOPE, sensor_cb,data,20);
		}
		if (is_supported(SENSOR_LIGHT)){
			sendMessage("Light Supported");
			add_listener(3,SENSOR_LIGHT, sensor_cb,data,100);
		}
		if (is_supported(SENSOR_HUMIDITY)){
			sendMessage("Humidity Supported");
			//add_listener(4,SENSOR_HUMIDITY, sensor_cb,data);
		}
		if (is_supported(SENSOR_MAGNETIC)){
			sendMessage("Magnetic Supported");
			//add_listener(5,SENSOR_MAGNETIC, sensor_cb,data);
		}
		if (is_supported(SENSOR_PRESSURE)){
			sendMessage("Pressure Supported");
			add_listener(6,SENSOR_PRESSURE, sensor_cb,data,100);
		}
		if (is_supported(SENSOR_TEMPERATURE)){
			sendMessage("Temp Supported");
			//add_listener(7,SENSOR_TEMPERATURE, sensor_cb,data);
		}

}


/*
 * Writes String to File
 * Should only be used for test purpose since fopen is called every time fputs should be called directly in callback
 */
void write_file(const char* filepath, const char* buf)
{
    FILE *fp;
    fp = fopen(filepath,"w");
    fputs(buf,fp);
    fclose(fp);
}


/*
 * Returns a writable and accessible path for date to be stored in with the given FileName
 */
char* get_write_filepath(char *filename)
{
    char *write_filepath[1000];
    char *resource_path = "/home/owner/media/Others/";// get the application data directory path
    mkdir(resource_path, 0777);
    if(resource_path)
    {
        sprintf(write_filepath,"%s%s",resource_path,filename);
    }

    return write_filepath;
}

/*
 * Stops/removes Sensor Listener
 */
void stopListener(void *data){
	appdata_s *ad = (appdata_s *) data;
	for (int i = 0; i < listenerSize; i++) {
			sensor_listener_stop(ad->listener[i]);
			sensor_destroy_listener(ad->listener[i]);
	}
}

/*
 * Not used anymore since the recording method has been change but might be useful in the future
 */
void print_error(int error){
	switch(error){
				case RECORDER_ERROR_INVALID_PARAMETER: sendMessage("Invalid parameter"); return;
				case RECORDER_ERROR_SOUND_POLICY: sendMessage("Sound Policy"); return;
				case RECORDER_ERROR_RESOURCE_CONFLICT: sendMessage("Recource Conflict"); return;
				case RECORDER_ERROR_PERMISSION_DENIED: sendMessage("The access to the resources can not be granted"); return;
				case RECORDER_ERROR_INVALID_OPERATION: sendMessage("Invalid operation"); return;
				case RECORDER_ERROR_INVALID_STATE: sendMessage("Invalid state"); return;
				case RECORDER_ERROR_NOT_SUPPORTED: sendMessage("not supported");
			}
}

/*
 * Raw audio input CB just needs to be written to the file hwich had been created in method start_recording
 */
void _audio_io_stream_read_cb(audio_in_h handle, size_t nbytes, void *userdata)
{
    const void * buffer = NULL;

    if (nbytes > 0) {
        /*
           Retrieve a pointer to the internal input buffer
           and the number of recorded audio data bytes
        */
        int error_code = audio_in_peek(handle, &buffer, &nbytes);
        if (error_code != AUDIO_IO_ERROR_NONE) {
            dlog_print(DLOG_ERROR, LOG_TAG, "audio_in_peek() failed! Error code = %d", error_code);

            return;
        }

        /* Store the recorded audio data in the file */
        fwrite(buffer, sizeof(char), nbytes, fp_audio);

        /* Remove the recorded audio data from the internal input buffer */
        error_code = audio_in_drop(handle);
        if (error_code != AUDIO_IO_ERROR_NONE)
            dlog_print(DLOG_ERROR, LOG_TAG, "audio_in_drop() failed! Error code = %d", error_code);
    }
}


/*
 * Writes initial amount of bytes for wav header to be overwritten at the end when all parameters are known.
 * because size = length of the recording need to be in the header...
 */
void write_fake_header(FILE *file){
	char bytes [44];
	for (int i =0 ; i< 43; i++){
		bytes[i] = 0;
	}

	fputs(bytes, file);

}


/*
 * starts recording audio
 */
void start_recording(void *data){

	//Create file for timestamp
	FILE * audio_stamp;
	char * stamp_path = get_write_filepath("audio.time");
	audio_stamp = fopen(stamp_path, "w");

	//setup Timestamp to save to audio.time file
	time_t raw_time;
	struct timespec time_spec;
	clock_gettime(CLOCK_REALTIME, &time_spec);
	unsigned long long ms = time_spec.tv_nsec / 1000000LL;
	time(&raw_time);
	struct tm* l_time = localtime(&raw_time);

	//write Timestamp
	char timeStamp[100] = "";
	sprintf(timeStamp, "%d-%02d-%02d %02d:%02d:%02d.%03llu",l_time->tm_year+1900,l_time->tm_mon+1,l_time->tm_mday,l_time->tm_hour,l_time->tm_min,l_time->tm_sec,ms);
	fputs(timeStamp, audio_stamp);
	fclose(audio_stamp);


	//Here comes now the audio File
	char * filename;
	filename = get_write_filepath("audio.wav");
	fp_audio = fopen(filename, "w");

	//setup raw audio input and callback
	audio_in_create(SAMPLE_RATE, AUDIO_CHANNEL_MONO, AUDIO_SAMPLE_TYPE_S16_LE, &input); //set up recorder for PCM recording audio
	audio_in_set_stream_cb(input, _audio_io_stream_read_cb, NULL); //set callback for recording
	audio_in_prepare(input); // starts the audio recording

}


/*
 * Prints all collected wifi devices
 * and sets Timestamp for them
 */

void print_wifi(){

	time_t raw_time;
	struct timespec time_spec;
	clock_gettime(CLOCK_REALTIME, &time_spec);
	unsigned long long ms = time_spec.tv_nsec / 1000000LL;
	time(&raw_time);
	struct tm* l_time = localtime(&raw_time);
	char timeStamp[100] = "";
	sprintf(timeStamp, "dBm %d-%02d-%02dT%02d:%02d:%02d.%03llu",l_time->tm_year+1900,l_time->tm_mon+1,l_time->tm_mday,l_time->tm_hour,l_time->tm_min,l_time->tm_sec,ms);
	GList *next = devices_list_wifi;
	wifi_s *data;
	while(next != NULL){
		data = next->data;
		char strength[100] = "";
		char out [1000] = "";
		strcat(out, data->bssid);
		strcat(out, " ");
		sprintf(strength, "%d",data->rssid);
		strcat(out, strength);
		strcat(out, timeStamp);
		strcat(out, "\r\n");
		fputs(out, fpwifi);
		next = next->next;
	}
}


/**
 * Print all scanned and found bluetooth devices and stamp with time
 */

void print_ble(){
	time_t raw_time;
	struct timespec time_spec;
	clock_gettime(CLOCK_REALTIME, &time_spec);
	unsigned long long ms = time_spec.tv_nsec / 1000000LL;
	time(&raw_time);
	struct tm* l_time = localtime(&raw_time);
	char timeStamp[100] = "";
	sprintf(timeStamp, "dBm %d-%02d-%02dT%02d:%02d:%02d.%03llu",l_time->tm_year+1900,l_time->tm_mon+1,l_time->tm_mday,l_time->tm_hour,l_time->tm_min,l_time->tm_sec,ms);
	GList *next = devices_list_ble;
	wifi_s *data;
	while (next != NULL){

		data = next->data;

		char strength[100] = "";
		char out [1000] = "";
		strcat(out, data->bssid);
		strcat(out, " ");
		sprintf(strength, "%d",data->rssid);
		strcat(out, strength);
		strcat(out, timeStamp);
		strcat(out, "\r\n");
		fputs(out, fpble);
		next = next->next;
	}
}


/*
 * Service app has been started,
 * 	Launch MessagePort for communication with host App
 * 	Start audio,BLE, WIFI and sensor recording
 */
void service_app_control(app_control_h app_control, void *data)
{
	//appdata_s *ad = (appdata_s *) data;
	char *operation;
	app_control_get_operation(app_control, &operation);
	if(!strcmp(operation, "start")){
		sendMessage("start Called");
		if (running) {
			sendMessage("is Already running");
			//TODO: send Time
			return;
		}
		//Create Message Channel
		message_port_register_local_port("MY_PORT", message_port_cb, data);
		//startListener(data);
		sendMessage("start recording");
		running = true;
		start_recording(data);
		startListener(data);
		//sendMessage("Audio and sensors recording");
		start_ble_scanner();
		start_wifi_scanner();
		start_timer();
		sendMessage("running Normal");
	}
	if(!strcmp(operation, "stop")){
		sendMessage("StopCalled: outdated"); //outdated; should not be called anymore
	}
    return;
}

int iteration = 0;
time_t raw_time_recorder;


/*
 * Timer to stop Recording after 8 hours
 */
Eina_Bool __time_recorder_cb(void *data){
	char time[10];
	struct tm* l_time = localtime(&raw_time_recorder);
	sprintf(time, "%02d:%02d:02d",l_time->tm_hour,l_time->tm_min,l_time->tm_sec);
	if (l_time->tm_hour == 8) {

		//stop_audio();
		//stopListener(user_data);
		//s
	}

	return ECORE_CALLBACK_RENEW;

}


/*
 * Callback for ble and wifi to be called at a specific amount of time (In this case it should be 5 senonds and every 10 seconds we safe the devices);
 * Safe Collected BLE and WIFI devices.
 *
 * !IMPORTANT the return value sets whether function gets called again or not
 */
Eina_Bool __timer_cb(void *data){
	if (!running) {
		//if we are not recording anymore stop the timer via return value
		return ECORE_CALLBACK_CANCEL;
	}
	if (iteration == 0) {
		//5 seconds over
		iteration++;
		bt_adapter_le_stop_scan();
		return ECORE_CALLBACK_RENEW;
	}
	//10 seconds over
	print_ble();
	print_wifi();
	devices_list_ble = NULL;
	devices_list_wifi = NULL;
	bt_adapter_le_start_scan(_bluetooth_le_cb, NULL);
	iteration--;
	wifi_scan(__scan_request_cb, NULL);

	return ECORE_CALLBACK_RENEW;
}

Ecore_Timer * timer;

Ecore_Timer * time_recorder;


void stop_timer(){

	ecore_timer_del(timer);
	ecore_timer_del(time_recorder);
	fclose(fpwifi);
	fclose(fpble);

}

/**
 * adds a timer to be called every 5 seconds
 */
void start_timer(){

	//time(&raw_time);

	timer = ecore_timer_add(5, __timer_cb, NULL);
	//time_recorder = ecore_timer_add(1, __time_recorder_cb, NULL);
	//raw_time_recorder = 0L;

}

static void
service_app_lang_changed(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_LANGUAGE_CHANGED*/
	return;
}

static void
service_app_region_changed(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_REGION_FORMAT_CHANGED*/
}

static void
service_app_low_battery(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_LOW_BATTERY*/
}

static void
service_app_low_memory(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_LOW_MEMORY*/
}

int main(int argc, char* argv[])
{
    char ad[50] = {0,};
	service_app_lifecycle_callback_s event_callback;
	app_event_handler_h handlers[5] = {NULL, };

	event_callback.create = service_app_create;
	event_callback.terminate = service_app_terminate;
	event_callback.app_control = service_app_control;

	service_app_add_event_handler(&handlers[APP_EVENT_LOW_BATTERY], APP_EVENT_LOW_BATTERY, service_app_low_battery, &ad);
	service_app_add_event_handler(&handlers[APP_EVENT_LOW_MEMORY], APP_EVENT_LOW_MEMORY, service_app_low_memory, &ad);
	service_app_add_event_handler(&handlers[APP_EVENT_LANGUAGE_CHANGED], APP_EVENT_LANGUAGE_CHANGED, service_app_lang_changed, &ad);
	service_app_add_event_handler(&handlers[APP_EVENT_REGION_FORMAT_CHANGED], APP_EVENT_REGION_FORMAT_CHANGED, service_app_region_changed, &ad);

	return service_app_main(argc, argv, &event_callback, ad);
}
