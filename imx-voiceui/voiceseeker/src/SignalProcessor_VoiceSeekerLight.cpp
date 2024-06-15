/*
 * Copyright (c) 2021 by Retune DSP.
 * Copyright 2022-2024 NXP
 *
 * NXP Confidential. This software is owned or controlled by NXP
 * and may only be used strictly in accordance with the applicable license terms.
 * By expressly accepting such terms or by downloading, installing,
 * activating and/or otherwise using the software, you are agreeing that you have read,
 * and that you agree to comply with and are bound by, such license terms.
 * If you do not agree to be bound by the applicable license terms,
 * then you may not retain, install, activate or otherwise use the software.
 */


#include "SignalProcessor_VoiceSeekerLight.h"

using namespace AFEConfig;

namespace SignalProcessor {

	const uint32_t delaySamples = 3211;

	const std::string SignalProcessor_VoiceSeekerLight::_jsonConfigDescription =
		"{\n\
            \"default_config\" : {\n\
            \"sample_rate\" : -1,\n\
            \"sample_format\" : S32_LE,\n\
            \"period_size\" : 800,\n\
            \"input_channels\" : 4,\n\
            \"ref_channels\" : 2,\n\
            \"channel2output\" : 0\n\
        },\n\
        \"valid_options\" : {\n\
            \"sample_format\" : [\"string\", \"enum\", [\"S8\", \"U8\", \"S16_LE\", \"S16_BE\", \"U16_LE\", \"U16_BE\", \"S24_LE\", \"S24_BE\", \"U24_LE\", \"U24_BE\", \"S32_LE\", \"S32_BE\", \"U32_LE\", \"U32_BE\", \"FLOAT_LE\", \"FLOAT_BE\", \"FLOAT64_LE\", \"FLOAT64_BE\"]],\n\
            \"period_size\" : [\"int\", \"range\", 0, 4096,\n\
            \"input_channels\" : [\"int\", \"range\", 1, 1024],\n\
            \"channel2output\" : [\"int\", \"range\", 0, \"input_channels_max\"]\n\
        }\n}";

	//Construtor, initializes internal resources
	SignalProcessor_VoiceSeekerLight::SignalProcessor_VoiceSeekerLight() : _state(VoiceSeekerLightSignalProcessorState::closed),
		sizeBuffDelay{ 128000 }, iteration{ 0 }, heap_size{ 512000 }, scratch_size{ 5120 }, heap_memory{nullptr},
		scratch_memory{ nullptr }, ref_in{ nullptr }, mic_in{ nullptr }, vsl{ 0 }, vsl_config{ 0 }, disable_trigger_frame_counter{0},
		num_delay_files{ 0 }, fid_delay_files_open{false}, debugEnable{ false }{

		printf("VoiceSeekerLight App v%i.%i.%i\n", RDSP_VOICESEEKER_LIGHT_APP_VERSION_MAJOR, RDSP_VOICESEEKER_LIGHT_APP_VERSION_MINOR, RDSP_VOICESEEKER_LIGHT_APP_VERSION_PATCH);

		setDefaultSettings(); //Initialize the _signalProcessorSettings to default values

		/*
		 * VoiceSeekerLight plugin configuration
		 */

		vsl_config.num_mics = this->_inputChannelsCount;
		vsl_config.num_spks = RDSP_ENABLE_AEC ? this->_referenceChannelsCount : 0;
		vsl_config.framesize_out = VOICESEEKER_OUT_NHOP;
		vsl_config.buffer_length_sec = RDSP_BUFFER_LENGTH_SEC;
		vsl_config.aec_filter_length_ms = RDSP_AEC_FILTER_LENGTH_MS;
		vsl_config.create_aec = RDSP_ENABLE_AEC;

#if defined CortexA55
		vsl_config.device_id = Device_IMX9_CA55;
#elif defined CortexA53
		vsl_config.device_id = Device_IMX8M_CA53;
#else
		vsl_config.device_id = Device_IMX8M_CA53;
#endif
		// Specify mic geometry
		vsl_config.mic_xyz_mm = (rdsp_xyz_t*)malloc(sizeof(rdsp_xyz_t) * vsl_config.num_mics);
		if (vsl_config.mic_xyz_mm == NULL) {
			printf("VoiceSeekerLight_Create: failed to allocate for mic geometry mem\n");
			return;
		}

		// mic0 xyz
		if (vsl_config.num_mics > 0) {
			vsl_config.mic_xyz_mm[0][0] = this->mic[0].x;
			vsl_config.mic_xyz_mm[0][1] = this->mic[0].y;
			vsl_config.mic_xyz_mm[0][2] = this->mic[0].z;
		}
		// mic1 xyz
		if (vsl_config.num_mics > 1) {
			vsl_config.mic_xyz_mm[1][0] = this->mic[1].x;
			vsl_config.mic_xyz_mm[1][1] = this->mic[1].y;
			vsl_config.mic_xyz_mm[1][2] = this->mic[1].z;
		}
		// mic2 xyz
		if (vsl_config.num_mics > 2) {
			vsl_config.mic_xyz_mm[2][0] = this->mic[2].x;
			vsl_config.mic_xyz_mm[2][1] = this->mic[2].y;
			vsl_config.mic_xyz_mm[2][2] = this->mic[2].z;
		}
		// mic3 xyz
		if (vsl_config.num_mics > 3) {
			vsl_config.mic_xyz_mm[3][0] = this->mic[3].x;
			vsl_config.mic_xyz_mm[3][1] = this->mic[3].y;
			vsl_config.mic_xyz_mm[3][2] = this->mic[3].z;
		}

		//Allocate memory
		uint32_t heap_req_bytes = VoiceSeekerLight_GetRequiredHeapMemoryBytes(&vsl, &vsl_config) + 200000;
		void* heap_memory = malloc(heap_req_bytes);
		if (heap_memory == NULL) {
			printf("VoiceSeekerLight_Create: failed to allocate for heap_memory\n");
			return;
		}

		void* scratch_memory = malloc(scratch_size);
		if (scratch_memory == NULL) {
			printf("VoiceSeekerLight_Create: failed to allocate for scratch_memory\n");
			return;
		}

		vsl.mem.pPrivateDataBase = heap_memory;
		vsl.mem.pPrivateDataNext = heap_memory;
		vsl.mem.FreePrivateDataSize = heap_req_bytes;
		vsl.mem.pScratchDataBase = scratch_memory;
		vsl.mem.pScratchDataNext = scratch_memory;
		vsl.mem.FreeScratchDataSize = scratch_size;

		//VoiceSeekerLight creation
		RdspStatus voiceseeker_status = VoiceSeekerLight_Create(&vsl, &vsl_config);
		if (voiceseeker_status != OK) {
			printf("VoiceSeekerLight_Create: voiceseeker_status = %d\n", voiceseeker_status);
            throw -1;
		}

		VoiceSeekerLight_Init(&vsl);					//VoiceSeekerLight initialization
		VoiceSeekerLight_GetConfig(&vsl, &vsl_config);	//Retrieve VoiceSeekerLight configuration

		VoiceSeekerLight_GetConstants(&vsl_constants);

		// Unpack configuration
		framesize_in = vsl_constants.framesize_in;
		framesize_out = vsl_config.framesize_out;
		/*
		* VoiceSeekerLight Version and configuration
		*/

		rdsp_voiceseekerlight_ver_struct_t vsl_version;
		VoiceSeekerLight_GetLibVersion(&vsl, &vsl_version);
		printf("VoiceSeekerLight_GetLibVersion: v%i.%i.%i\n", vsl_version.major, vsl_version.minor, vsl_version.patch);
		VoiceSeekerLight_PrintConfig(&vsl); //Print VoiceSeekerLight configuration
		VoiceSeekerLight_PrintMemOverview(&vsl); //Print VoiceSeekerLight memory overview

		//Map mic_in pointers to mic buffer
		framesize_in_mic = framesize_in;
		float* mic_buffer = (float*)malloc(sizeof(float) * framesize_in_mic * this->_inputChannelsCount);
		mic_in = (float**)malloc(sizeof(float*) * this->_inputChannelsCount);
		for (int32_t imic = 0; imic < this->_inputChannelsCount; imic++) {
			mic_in[imic] = mic_buffer + imic * framesize_in_mic;
		}

		//Map ref_in pointers to ref buffer
		framesize_in_ref = framesize_in;
		float* ref_buffer = (float*)malloc(sizeof(float) * framesize_in_ref * this->_referenceChannelsCount);
		ref_in = (float**)malloc(sizeof(float*) * this->_referenceChannelsCount);
		for (int32_t ispk = 0; ispk < this->_referenceChannelsCount; ispk++) {
			ref_in[ispk] = ref_buffer + ispk * framesize_in_ref;
		}

		//Initialize buffer for delay
		initQueue(&circularBuffDelay, this->delaySamples, sizeBuffDelay);

		if(this->debugEnable)
			std::cout << "->SAVING AUDIO FILES FOR DELAY DEBUG ACTIVATED<-" << std::endl;
	}

	int32_t SignalProcessor_VoiceSeekerLight::openProcessor(const std::unordered_map<std::string, std::string>* settings) {

		if (VoiceSeekerLightSignalProcessorState::closed != this->_state) {
			std::cout << "Instance is already opened! It needs to be closed first." << std::endl;
			return -1;
		}

		// Overwrite default settings in case the user provided his own configuration
		if (nullptr != settings) {
			std::string format = settings->at("sample_format");
			snd_pcm_format_t formatValue = snd_pcm_format_value(format.c_str());
			if (SND_PCM_FORMAT_UNKNOWN != formatValue) {
				this->_sampleFormat = formatValue;
				/* Get the size of bytes for one sample for given format */
				this->_sampleSize = snd_pcm_format_size(this->_sampleFormat, 1);
			}
			else {
				closeProcessor();
				throw; /* TODO throw some meaningful exception */
			}

			this->_channel2output = stoi(settings->at("channel2output"));
			this->_inputChannelsCount = stoi(settings->at("input_channels"));
			this->_periodSize = stoi(settings->at("period_size"));

			vsl_config.num_mics = this->_inputChannelsCount;
			VoiceSeekerLight_GetConfig(&vsl, &vsl_config);	//Retrieve VoiceSeekerLight configuration
		}

		/*
		 * We can check the validity of settings here
		*/

		// Check that the selected output channel is in range of available channels
		if (this->_channel2output >= this->_inputChannelsCount)
			throw; /* TODO throw some meaningful exception */

		// To avoid opening the signal processor multiple times, we set a "state"
		this->_state = VoiceSeekerLightSignalProcessorState::opened;

		return 0;
	}

	int32_t SignalProcessor_VoiceSeekerLight::closeProcessor() {

		//Close files for delay debug
		if(this->debugEnable)
		{
			if (fid_delay_files_open) {
				rdsp_wav_close(&fid_mic_delay);
				rdsp_wav_close(&fid_ref_delay);
				rdsp_wav_close(&fid_mic_out);
				fid_delay_files_open = false;
			}
		}

		if (VoiceSeekerLightSignalProcessorState::filtering != this->_state) {
			queueDestroy(&circularBuffDelay); //Destroy circular buffer

			//Deallocate internal resources
			free(heap_memory);
			free(scratch_memory);
			free(mic_in);
			free(ref_in);
			// Free microphone geometry
			free(vsl_config.mic_xyz_mm);
			setDefaultSettings();
			this->_state = VoiceSeekerLightSignalProcessorState::closed;
			return 0;
		}

		return -1;
	}

	int32_t SignalProcessor_VoiceSeekerLight::processSignal(const char* nChannelMicBuffer, size_t micBufferSize,
		const char* nChannelRefBuffer, size_t refBufferSize,
		char* cleanMicBuffer, size_t cleanMicBufferSize) {

		//Lets check that the size of buffer matches the input settings. Provided size must match the configured
		size_t expectedBufferSize = this->_inputChannelsCount * this->_periodSize * this->_sampleSize;
		if (micBufferSize != expectedBufferSize) {
			std::cout << "Input buffer size doesn't match. Expected: " << expectedBufferSize << "; Got: " << micBufferSize << std::endl;
			return -1;
		}

		expectedBufferSize = this->_referenceChannelsCount * this->_periodSize * this->_sampleSize;
		if (refBufferSize != expectedBufferSize) {
			std::cout << "Reference buffer size doesn't match. Expected: " << expectedBufferSize << "; Got: " << refBufferSize << std::endl;
			return -2;
		}

		expectedBufferSize = this->_periodSize * this->_sampleSize;
		if (cleanMicBufferSize != expectedBufferSize) {
			std::cout << "output buffer size doesn't match" << std::endl;
			return -3;
		}

		if(this->debugEnable)
		{
			//Open file for saving audios
			if (!fid_delay_files_open && (num_delay_files * 1200 * MINUTE_INTERVAL_WAV_FILE == iteration)) {
				fid_delay_files_open = true;
				int start_min = iteration / 1200;
				int end_min = (iteration + 1200) / 1200;
				std::string refDelayName = "/tmp/ref_in_delay_S" + std::to_string(start_min) + "_E" + std::to_string(end_min) + ".wav";
				std::string micDelayName = "/tmp/mic_in_delay_S" + std::to_string(start_min) + "_E" + std::to_string(end_min) + ".wav";
				std::string micOutName = "/tmp/mic_out_S" + std::to_string(start_min) + "_E" + std::to_string(end_min) + ".wav";

				fid_ref_delay = rdsp_wav_write_open(refDelayName.c_str(), vsl_constants.samplerate, this->_referenceChannelsCount, this->_sampleSize * 8, WAVE_FORMAT_PCM);
				fid_mic_delay = rdsp_wav_write_open(micDelayName.c_str(), vsl_constants.samplerate, this->_inputChannelsCount, this->_sampleSize * 8, WAVE_FORMAT_PCM);
				fid_mic_out = rdsp_wav_write_open(micOutName.c_str(), vsl_constants.samplerate, 1, this->_sampleSize * 8, WAVE_FORMAT_PCM);
			}
		}

		iteration++;

		const int32_t framerate_out = this->_sampleRate / framesize_out;

		//Check if triggering is allowed
		int32_t enable_triggering = 1;
		if (disable_trigger_frame_counter > 0)
			enable_triggering = 0;
		--disable_trigger_frame_counter;

		// Let's process the signal - meaning copy the selected channel to output.
		this->_state = VoiceSeekerLightSignalProcessorState::filtering;
		int32_t shift = this->_inputChannelsCount * this->_sampleSize;

		char* pcleanMicBuffer = cleanMicBuffer;
		const char* pnChannelMicBuffer = nChannelMicBuffer;
		const char* pnChannelRefBuffer = nChannelRefBuffer;

		//Copy pnChannelRefBuffer to circularBuffDelay
		enqueue(&circularBuffDelay, pnChannelRefBuffer, refBufferSize);

		//Get samples from circularBuffDelay which has the delayed samples
		char* tmp_delayedRefBuffer = (char*)malloc(refBufferSize);
		char* delayedRefBuffer = tmp_delayedRefBuffer;
		dequeue(&circularBuffDelay, delayedRefBuffer, refBufferSize);

		char* tmp_buf = (char*)malloc(VOICESEEKER_OUT_NHOP * this->_sampleSize);

		for (int32_t j = 0; j < this->_periodSize / framesize_in_mic; j++) {

			rdsp_pcm_to_float(delayedRefBuffer, ref_in, framesize_in_ref, this->_referenceChannelsCount, this->_sampleSize);
			rdsp_pcm_to_float(pnChannelMicBuffer, mic_in, framesize_in_mic, this->_inputChannelsCount, this->_sampleSize);

			//Write to file for delay debug for 1 minute
			if(this->debugEnable)
			{
				if (fid_delay_files_open) {
					rdsp_wav_write_interleaved_int32((int32_t *)pnChannelMicBuffer, vsl_constants.framesize_in, &fid_mic_delay);
					rdsp_wav_write_interleaved_int32((int32_t *)delayedRefBuffer, vsl_constants.framesize_in, &fid_ref_delay);
				}
			}

			/*
			 * VOICESEEKER LIGHT PROCESS
			*/
			float* vsl_out = NULL;
			RdspStatus voiceseeker_status = VoiceSeekerLight_Process(&vsl, mic_in, ref_in, &vsl_out);
			if (voiceseeker_status != OK) {
				printf("VoiceSeekerLight_Process: voiceseeker_status = %d\n", (int32_t)voiceseeker_status);
				return -1;
			}

			// Check for output
			if (vsl_out != NULL) {
				rdsp_float_to_pcm(tmp_buf, &vsl_out, VOICESEEKER_OUT_NHOP, 1, this->_sampleSize);
				memcpy(pcleanMicBuffer, tmp_buf, this->_sampleSize * VOICESEEKER_OUT_NHOP);

				if(this->debugEnable)
				{
					if (fid_delay_files_open) {
						rdsp_wav_write_interleaved_int32((int32_t *)tmp_buf, VOICESEEKER_OUT_NHOP, &fid_mic_out);
					}
				}

				pcleanMicBuffer += (VOICESEEKER_OUT_NHOP * this->_sampleSize);
				if (this->_WWDetection) {
					sendBufferToWakeWordEngine(vsl_out, VOICESEEKER_OUT_NHOP * sizeof(float), iteration, enable_triggering);
					int32_t keyword_start_offset_samples = getKeyWordOffsetFromWakeWordEngine();
					if (keyword_start_offset_samples) {
						VoiceSeekerLight_TriggerFound(&vsl, keyword_start_offset_samples);

						//Don't allow re-triggering immediately after a trigger
						disable_trigger_frame_counter = RDSP_DISABLE_TRIGGER_TIMEOUT_SEC * framerate_out;
					}
				}
			}

			pnChannelMicBuffer += (this->_channel2output + vsl_constants.framesize_in * shift);
			delayedRefBuffer += (vsl_constants.framesize_in * this->_referenceChannelsCount * this->_sampleSize);
		}

		free(tmp_buf);
		free(tmp_delayedRefBuffer);

		if(this->debugEnable)
		{
			if (fid_delay_files_open && ((num_delay_files * 1200 * MINUTE_INTERVAL_WAV_FILE) + 1200 == iteration)) {
				rdsp_wav_close(&fid_mic_delay);
				rdsp_wav_close(&fid_ref_delay);
				rdsp_wav_close(&fid_mic_out);
				fid_delay_files_open = false;
				num_delay_files++;
			}
		}

		this->_state = VoiceSeekerLightSignalProcessorState::opened;
		return 0;
	}

	const std::string& SignalProcessor_VoiceSeekerLight::getJsonConfigurations() const {
		return _jsonConfigDescription;
	}

	int32_t	SignalProcessor_VoiceSeekerLight::getSampleRate() const {
		return this->_sampleRate;
	}

	const char* SignalProcessor_VoiceSeekerLight::getSampleFormat() const {
		return snd_pcm_format_name(this->_sampleFormat);
	}

	int32_t SignalProcessor_VoiceSeekerLight::getPeriodSize() const {
		return this->_periodSize;
	}

	int32_t SignalProcessor_VoiceSeekerLight::getInputChannelsCount() const {
		return this->_inputChannelsCount;
	}

	int32_t SignalProcessor_VoiceSeekerLight::getReferenceChannelsCount() const {
		return this->_referenceChannelsCount;
	}

	uint32_t SignalProcessor_VoiceSeekerLight::getVersionNumber() const {
		return (uint32_t)((uint32_t)1 << 24); //version 1.0.0
	}

	//Private section
	void SignalProcessor_VoiceSeekerLight::setDefaultSettings() {
		this->_sampleRate = 16000;
		this->_sampleFormat = SND_PCM_FORMAT_S32_LE; //Number should correspond to ALSA formats
		this->_periodSize = 800;
		this->_inputChannelsCount = 4; //Number of mic channels
		this->_referenceChannelsCount = 2; //Number of reference channels (speakers)
		this->_channel2output =	0; //By default use first channel as output
		this->_sampleSize = snd_pcm_format_size(this->_sampleFormat, 1); //Derive sample size from sample format

		AFEConfigState configState;
		this->_WWDetection = (configState.isConfigurationEnable("WWDectionDisable", 0) == 1)? false : true;
		this->delaySamples = configState.isConfigurationEnable("RefSignalDelay", delaySamples);
		this->debugEnable = (configState.isConfigurationEnable("DebugEnable", 0) == 1)? true : false;
		/*
			mic0 = 35.0, 15.15, 0.0
			mic1 = 17.5, -15.15, 0.0
			mic2 = -17.5, -15.15, 0.0
			mic3 = -35.0, 15.15, 0.0
		*/
		mic_xyz micDefaultState[4] = {{35.0, 15.15, 0.0}, {17.5, -15.15, 0.0}, {-17.5, -15.15, 0.0}, {-35.0, 15.15, 0.0}};
		machine_info = getMachineInfo();
		switch (machine_info) {
		case MACHINE_IMX8M:
			this->mic[0] = micDefaultState[0];
			this->mic[1] = micDefaultState[1];
			this->mic[2] = micDefaultState[2];
			this->mic[3] = micDefaultState[3];
			break;
		case MACHINE_IMX93EVK11:
			this->mic[0] = {59.5, -48.0, 0.0};
			this->mic[1] = {-59.5, -52.5, 0.0};
			this->mic[2] = {59.5, 48.0, 0.0};
			this->mic[3] = {-59.5, 52.5, 0.0};
			break;
		case MACHINE_IMX93QSB:
			this->mic[0] = {36.0, -36.0, 0};
			this->mic[1] = {-36.0, -36.0, 0};
			this->mic[2] = {36.0, 36.0, 0};
			this->mic[3] = {-36.0, 36, 0};
			break;
		default:
			this->mic[0] = configState.isConfigurationEnable("mic0", micDefaultState[0]);
			this->mic[1] = configState.isConfigurationEnable("mic1", micDefaultState[1]);
			this->mic[2] = configState.isConfigurationEnable("mic2", micDefaultState[2]);
			this->mic[3] = configState.isConfigurationEnable("mic3", micDefaultState[3]);
		}
		for (int i = 0; i < 4; i++)
		{
			std::cout << "mic" << i << " xyz: (" << mic[i].x << ", " << mic[i].y << ", " << mic[i].z << ")" << std::endl;
		}
		std::cout << "delayValue " << this->delaySamples << " debugValue " << this->debugEnable << " WakeWord Detection " << this->_WWDetection
			<< std::endl;
	}

	void SignalProcessor_VoiceSeekerLight::initQueue(queue* q, size_t samples_delay, size_t maxSize) {
		q->size = maxSize;
		q->samples = (char*)malloc(q->size);
		q->num_entries = samples_delay * this->_referenceChannelsCount * this->_sampleSize;
		q->head = 0;
		q->tail = samples_delay * this->_referenceChannelsCount * this->_sampleSize;

		//Initialize buffer to zeros
		memset(q->samples, 0, q->size);
	}

	void SignalProcessor_VoiceSeekerLight::queueDestroy(queue* q) {
		free(q->samples);
	}

	void SignalProcessor_VoiceSeekerLight::enqueue(queue* q, const char* samples_ref, size_t sizeBuff) {
		int32_t avail_bytes = q->size - q->tail;

		if ((q->size - q->num_entries) < sizeBuff) {
			std::cout << "enqueue error" << std::endl;
			return;
		}

		if (avail_bytes >= sizeBuff) {
			memcpy(q->samples + q->tail, (char*)samples_ref, sizeBuff);
			q->tail = (q->tail + sizeBuff) % q->size;
			q->num_entries += sizeBuff;
		}
		else {
			memcpy((q->samples) + q->tail, (char*)samples_ref, avail_bytes);
			q->tail = 0;
			int32_t rest_of_samples = sizeBuff - avail_bytes;
			memcpy((q->samples) + q->tail, (char*)samples_ref + avail_bytes, rest_of_samples);
			q->tail = (q->tail + rest_of_samples) % q->size;
			q->num_entries += sizeBuff;
		}
	}

	void SignalProcessor_VoiceSeekerLight::dequeue(queue* q, char * samples, size_t sizeBuff) {

		if (sizeBuff > q->num_entries) {
			memset(samples, 0, sizeBuff);
			std::cout << "dequeue error" << std::endl;
			return;
		}

		if ((q->size - q->head) >= sizeBuff) {
			memcpy(samples, q->samples + q->head, sizeBuff);
			q->head = (q->head + sizeBuff) % q->size;
			q->num_entries -= sizeBuff;
		} else {
			int32_t avail_bytes = (q->size - q->head);
			memcpy(samples, q->samples + q->head, avail_bytes);
			q->head = 0;
			int32_t rest_of_samples = sizeBuff - avail_bytes;
			memcpy(samples + avail_bytes, (q->samples) + q->head, rest_of_samples);
			q->head = rest_of_samples;
			q->num_entries -= sizeBuff;
		}
	}

	int32_t SignalProcessor_VoiceSeekerLight::sendBufferToWakeWordEngine(void* buffer, int32_t length, int32_t iteration, int32_t enable_triggering) {
		mqd_t mq;
		mqd_t iter;
		mqd_t trigg;

		// Open the mail queue
		mq = mq_open("/voicespot_vslout", O_WRONLY);
		iter = mq_open("/voiceseeker_iterations", O_WRONLY);
		trigg = mq_open("/voiceseeker_trigger", O_WRONLY);
		CHECK((mqd_t)-1 != mq);
		CHECK((mqd_t)-1 != iter);
		CHECK((mqd_t)-1 != trigg);

		// End the message
		CHECK(0 <= mq_send(mq, (char*)buffer, length, 0));
		CHECK(0 <= mq_send(iter, (char*)&iteration, sizeof(int32_t), 0));
		CHECK(0 <= mq_send(trigg, (char*)&enable_triggering, sizeof(int32_t), 0));

		// Cleanup
		mq_close(mq);
		mq_close(iter);
		mq_close(trigg);

		return 0;
	}

	int32_t SignalProcessor_VoiceSeekerLight::getKeyWordOffsetFromWakeWordEngine() {
		mqd_t mq;
		int32_t offset;
		int32_t bytes_read;

		// Open the mail queue
		mq = mq_open("/voicespot_offset", O_RDONLY);
		CHECK((mqd_t)-1 != mq);

		// Send the message
		bytes_read = mq_receive(mq, (char*)&offset, sizeof(int32_t), NULL);

		// Cleanup
		mq_close(mq);

		return offset;
	}

	MachineInfo SignalProcessor_VoiceSeekerLight::getMachineInfo() {
		MachineInfo machine = MACHINE_UNKNOWN;
		char buf[64] = {0};
		FILE* fileptr;

		fileptr = fopen("/sys/devices/soc0/machine", "rb");
		if (fileptr == NULL)
			return MACHINE_UNKNOWN;
		else if (fread(buf, sizeof(char), sizeof(buf), fileptr)) {
			if (strstr(buf, "i.MX8M"))
				machine = MACHINE_IMX8M;
			else if (strstr(buf, "i.MX93 11"))
				machine = MACHINE_IMX93EVK11;
			else if (strstr(buf, "i.MX93 9"))
				machine = MACHINE_IMX93QSB;
		}
		fclose(fileptr);
		return machine;
	}
}

/*
This is a mandatory interface. We need to get an instance of our class, however, we can't
export a Constructor/Destructor. So we create these C functions, which can be dynamically loaded
(their names don't get mangeled, as they are in C, not C++). These return us a pointer to our
instance, which can be used to invoke the rest of functions defined in the implementation.
*/
extern "C"  /* !!!We need to define these functions as extern "C", so their names don't get mangeled and we can load them dynamically!!! */
{
	SignalProcessor::SignalProcessorImplementation* createProcessor() {
        SignalProcessor::SignalProcessor_VoiceSeekerLight* vs;
        try {
		    vs =  new SignalProcessor::SignalProcessor_VoiceSeekerLight();
        }
        catch (int error) {
            vs =  NULL;
        }

        return vs;
	}

	void destroyProcessor(SignalProcessor::SignalProcessorImplementation* processorHandle) {
		delete processorHandle;
	}
}
