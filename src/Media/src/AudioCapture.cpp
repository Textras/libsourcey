//
// LibSourcey
// Copyright (C) 2005, Sourcey <http://sourcey.com>
//
// LibSourcey is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// LibSourcey is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//


#include "Sourcey/Media/AudioCapture.h"
#include "Sourcey/Logger.h"


using namespace std;
using namespace Poco;
using namespace scy;


namespace scy {
namespace av {


AudioCapture::AudioCapture(int deviceId, int channels, int sampleRate, RtAudioFormat format) : 
	_deviceId(deviceId),
	_channels(channels),
	_sampleRate(sampleRate),
	_format(format),
	_isOpen(false)
{
	traceL("AudioCapture", this) << "Creating" << endl;

	_iParams.deviceId = _deviceId;
	_iParams.nChannels = _channels;
	_iParams.firstChannel = 0;

	if (_audio.getDeviceCount() < 1) {
		warnL("AudioCapture", this) << "No audio devices found!" << endl;
		return;
	}

	// Let RtAudio print messages to stderr.
	_audio.showWarnings(true);
		
	// Open the audio stream or throw an exception.
	open(); //channels, sampleRate
	traceL("AudioCapture", this) << "Creating: OK" << endl;
}


AudioCapture::~AudioCapture()
{
	traceL("AudioCapture", this) << "Destroying" << endl;
}


void AudioCapture::open() //int channels, int sampleRate, RtAudioFormat format
{
	if (isOpen())
		close();

	FastMutex::ScopedLock lock(_mutex);
	traceL("AudioCapture", this) << "Opening: " << _channels << ": " << _sampleRate << endl;
	
	//_channels = channels;
	//_sampleRate = sampleRate;
	//_format = format;
	//_iParams.nChannels = _channels;
	unsigned int nBufferFrames = 256; //512;

	try {
		_audio.openStream(NULL, &_iParams, _format, _sampleRate, &nBufferFrames, &AudioCapture::callback, (void*)this);
		_error = "";
		_isOpen = true;
		traceL("AudioCapture", this) << "Opening: OK" << endl;
	}
	catch (RtError& e) {
		setError("Cannot open audio capture: " + e.getMessage());
	}
	catch (...) {
		setError("Cannot open audio capture.");
	}
}


void AudioCapture::close()
{	
	traceL("AudioCapture", this) << "Closing" << endl;
	try {
		FastMutex::ScopedLock lock(_mutex);
		_isOpen = false;
		if (_audio.isStreamOpen())
			_audio.closeStream();
		traceL("AudioCapture", this) << "Closing: OK" << endl;
	}
	catch (RtError& e) {
		setError("Cannot close audio capture: " + e.getMessage());
	}
	catch (...) {
		setError("Cannot close audio capture.");
	}
}


void AudioCapture::start()
{	
	traceL("AudioCapture", this) << "Starting" << endl;

	if (!isRunning()) {
		try {
			FastMutex::ScopedLock lock(_mutex);
			_audio.startStream();
			_error = "";
			traceL("AudioCapture", this) << "Starting: OK" << endl;
		}
		catch (RtError& e) {
			setError("Cannot start audio capture: " + e.getMessage());
		}
		catch (...) {
			setError("Cannot start audio capture.");
		}
	}
}


void AudioCapture::stop()
{	
	traceL("AudioCapture", this) << "Stopping" << endl;

	if (isRunning()) {
		try {
			FastMutex::ScopedLock lock(_mutex);
			traceL("AudioCapture", this) << "Stopping: Before" << endl;
			_audio.stopStream();
			traceL("AudioCapture", this) << "Stopping: OK" << endl;
		}
		catch (RtError& e) {
			setError("Cannot stop audio capture: " + e.getMessage());
		}
		catch (...) {
			setError("Cannot stop audio capture.");
		}
	}
}


void AudioCapture::attach(const PacketDelegateBase& delegate)
{
	PacketEmitter::attach(delegate);
	debugL("AudioCapture", this) << "Added Delegate: " << refCount() << endl;
	if (refCount() == 1)
		start();
}


bool AudioCapture::detach(const PacketDelegateBase& delegate) 
{
	if (PacketEmitter::detach(delegate)) {
		debugL("AudioCapture", this) << "Removed Delegate: " << refCount() << endl;
		if (refCount() == 0)
			stop();
		debugL("AudioCapture", this) << "Removed Delegate: OK" << endl;
		return true;
	}
	return false;
}


void AudioCapture::setError(const string& message)
{
	_error = message;
	errorL("AudioCapture", this) << "Error: " << message << endl;
	throw Exception(message);
}


int AudioCapture::callback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
	double streamTime, RtAudioStreamStatus status, void* data)
{
	AudioCapture* klass = (AudioCapture*)data;
	
	if (status) 
		errorL("AudioCapture", klass) << "Stream over/underflow detected" << endl;

	assert(inputBuffer != NULL);
	if (inputBuffer == NULL) {
		errorL("AudioCapture", klass) << "Input buffer is NULL." << endl;
		return 2;
	} 

	//if (!klass->isOpen())
	//	return 2;

	int size = 2;
	RtAudioFormat format = klass->format();
	// - \e RTAUDIO_SINT8:   8-bit signed integer.
	if (format == RTAUDIO_SINT8)
		size = 1;
    // - \e RTAUDIO_SINT16:  16-bit signed integer.
	else if (format == RTAUDIO_SINT16)
		size = 2;
    // - \e RTAUDIO_SINT24:  Lower 3 bytes of 32-bit signed integer.
	else if (format == RTAUDIO_SINT24)
		size = 4;
    // - \e RTAUDIO_SINT32:  32-bit signed integer.
	else if (format == RTAUDIO_SINT32)
		size = 4;
    // - \e RTAUDIO_FLOAT32: Normalized between plus/minus 1.0.
	else if (format == RTAUDIO_FLOAT32)
		size = 4;
    // - \e RTAUDIO_FLOAT64: Normalized between plus/minus 1.0.
	else if (format == RTAUDIO_FLOAT64)
		size = 8;
	else assert(0 && "unknown audio capture format");

	AudioPacket packet((const char*)inputBuffer, 
		nBufferFrames * klass->numChannels() * size, //sizeof(AUDIO_DATA),
		(double)streamTime);

	//traceL() << "[AudioCapture] AudioPacket: " 
	//	<< "\n\tPacket Ptr: " << inputBuffer
	//	<< "\n\tPacket Size: " << packet.size() 
	//	<< "\n\tStream Time: " << packet.time
	//	<< endl;

	klass->emit(klass, packet);
	//traceL("AudioCapture", klass) << "Callback: OK" << endl;
	return 0;
}


RtAudioFormat AudioCapture::format() const
{ 
	FastMutex::ScopedLock lock(_mutex);
	return _format;
}


bool AudioCapture::isOpen() const
{ 
	FastMutex::ScopedLock lock(_mutex);
	return _isOpen;
}


bool AudioCapture::isRunning() const
{
	FastMutex::ScopedLock lock(_mutex);
	return _audio.isStreamRunning();
}


int AudioCapture::deviceId() const 
{
	FastMutex::ScopedLock lock(_mutex);
	return _deviceId;
}


int AudioCapture::sampleRate() const 
{
	FastMutex::ScopedLock lock(_mutex);
	return _sampleRate;
}


int AudioCapture::numChannels() const 
{
	FastMutex::ScopedLock lock(_mutex);
	return _channels;
}


} } // namespace scy::av