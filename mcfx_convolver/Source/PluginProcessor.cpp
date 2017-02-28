/*
 ==============================================================================
 
 This file is part of the mcfx (Multichannel Effects) plug-in suite.
 Copyright (c) 2013/2014 - Matthias Kronlachner
 www.matthiaskronlachner.com
 
 Permission is granted to use this software under the terms of:
 the GPL v2 (or any later version)
 
 Details of these licenses can be found at: www.gnu.org/licenses
 
 mcfx is distributed in the hope that it will be useful, but WITHOUT ANY
 WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 
 ==============================================================================
 */

#include "PluginProcessor.h"
#include "PluginEditor.h"

#ifdef _WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif

#define CONVPROC_SCHEDULER_PRIORITY 0
#define CONVPROC_SCHEDULER_CLASS SCHED_FIFO
#define THREAD_SYNC_MODE true

#define MAX_PART_SIZE 8192
//==============================================================================
Mcfx_convolverAudioProcessor::Mcfx_convolverAudioProcessor() :
_min_in_ch(0),
_min_out_ch(0),
_num_conv(0),
_MaxPartSize(MAX_PART_SIZE),
_ConvBufferPos(0),
_isProcessing(false),
_configLoaded(false),
_skippedCycles(0),
Thread("mtx_convolver_master")

{
    _SampleRate = getSampleRate();
    _BufferSize = getBlockSize();
    _ConvBufferSize = getBlockSize();
    
    presetDir = presetDir.getSpecialLocation(File::userApplicationDataDirectory).getChildFile("mcfx/convolver_presets");
    std::cout << "Search dir:" << presetDir.getFullPathName() << std::endl;
    
	String debug;
    debug << "Search dir: " << presetDir.getFullPathName() << "\n\n";
    
    DebugPrint(debug);
    
    SearchPresets(presetDir);
    
    
    // this is for the open dialog of the gui
    lastDir = lastDir.getSpecialLocation(File::userHomeDirectory);
}

Mcfx_convolverAudioProcessor::~Mcfx_convolverAudioProcessor()
{
#ifdef USE_ZITA_CONVOLVER
    zita_conv.stop_process();
    zita_conv.cleanup();
#else
    mtxconv_.StopProc();
    mtxconv_.Cleanup();
#endif
}

//==============================================================================
const String Mcfx_convolverAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

int Mcfx_convolverAudioProcessor::getNumParameters()
{
    return 0;
}

float Mcfx_convolverAudioProcessor::getParameter (int index)
{
    return 0.0f;
}

void Mcfx_convolverAudioProcessor::setParameter (int index, float newValue)
{
}

const String Mcfx_convolverAudioProcessor::getParameterName (int index)
{
    return String::empty;
}

const String Mcfx_convolverAudioProcessor::getParameterText (int index)
{
    return String::empty;
}

const String Mcfx_convolverAudioProcessor::getInputChannelName (int channelIndex) const
{
    return String (channelIndex + 1);
}

const String Mcfx_convolverAudioProcessor::getOutputChannelName (int channelIndex) const
{
    return String (channelIndex + 1);
}

bool Mcfx_convolverAudioProcessor::isInputChannelStereoPair (int index) const
{
    return true;
}

bool Mcfx_convolverAudioProcessor::isOutputChannelStereoPair (int index) const
{
    return true;
}

bool Mcfx_convolverAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool Mcfx_convolverAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool Mcfx_convolverAudioProcessor::silenceInProducesSilenceOut() const
{
    return false;
}

double Mcfx_convolverAudioProcessor::getTailLengthSeconds() const
{
    if (_configLoaded)
    {
        // TODO: return length...
        return 0.0;
    } else {
        return 0.0;
    }
}

int Mcfx_convolverAudioProcessor::getNumPrograms()
{
    return 0;
}

int Mcfx_convolverAudioProcessor::getCurrentProgram()
{
    return 0;
}

void Mcfx_convolverAudioProcessor::setCurrentProgram (int index)
{
}

const String Mcfx_convolverAudioProcessor::getProgramName (int index)
{
    return String::empty;
}

void Mcfx_convolverAudioProcessor::changeProgramName (int index, const String& newName)
{
}

//==============================================================================
void Mcfx_convolverAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    
    if (_SampleRate != sampleRate || _BufferSize != samplesPerBlock)
    {
        _SampleRate = sampleRate;
        _BufferSize = samplesPerBlock;
        
        ReloadConfiguration();
    }
    
    if (_configLoaded)
    {
        // mtxconv_.Reset();
    }
    
}

void Mcfx_convolverAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

void Mcfx_convolverAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    
    // std::cout << "in: " << getTotalNumInputChannels() << " out: " << getTotalNumOutputChannels() << std::endl;
    
    if (_configLoaded)
    {
        
        _isProcessing = true;
        
#ifdef USE_ZITA_CONVOLVER
        
        for (int i=0; i < jmin(conv_data.getNumInputChannels(), getTotalNumInputChannels()) ; i++)
        {
            float* indata = zita_conv.inpdata(i)+_ConvBufferPos;
            memcpy(indata, buffer.getReadPointer(i), getBlockSize()*sizeof(float));
        }
        
        _ConvBufferPos += getBlockSize();
        
        if (_ConvBufferPos >= _ConvBufferSize) {
            zita_conv.process(THREAD_SYNC_MODE);
            _ConvBufferPos = 0;
        }
        
        
        
        for (int i=0; i < jmin(conv_data.getNumOutputChannels(), getNumOutputChannels()) ; i++)
        {
            float* outdata = zita_conv.outdata(i)+_ConvBufferPos;
            memcpy(buffer.getWritePointer(i), outdata, getBlockSize()*sizeof(float));
        }
        
#else
        //mtxconv_.processBlock(buffer, buffer, isNonRealtime()); // if isNotRealtime always set to true!
        mtxconv_.processBlock(buffer, buffer, true); // try to always wait except - add a special flag to deactivate waiting...

        _skippedCycles.set(mtxconv_.getSkipCount());
#endif
        
        _isProcessing = false;
        
    } else { // config loaded
        
        // clear output in case no config is loaded!
        buffer.clear();
    }
    

}

void Mcfx_convolverAudioProcessor::run()
{
    LoadConfiguration(_desConfigFile);
}

void Mcfx_convolverAudioProcessor::LoadConfigurationAsync(File configFile)
{
    DebugPrint("Loading preset...\n\n");
    _desConfigFile = configFile;
    startThread(6); // medium priority
}

void Mcfx_convolverAudioProcessor::ReloadConfiguration()
{
    if (_configLoaded || activePreset.isNotEmpty())
        LoadConfigurationAsync(_configFile);
}

void Mcfx_convolverAudioProcessor::LoadConfiguration(File configFile)
{
    if (!configFile.existsAsFile())
    {
        
        String debug;
        debug << "Configuration file does not exist!" << configFile.getFullPathName() << "\n\n";
        
        //std::cout << debug << std::endl;
        DebugPrint(debug);
        
        return;
    }
    
    // unload first....
    if (_configLoaded) {
        
        while (_isProcessing) {
            Sleep(1);
        }
        
        std::cout << "Unloading Config..." << std::endl;
        UnloadConfiguration();
        _DebugText = String(); // clear debug window
        std::cout << "Config Unloaded..." << std::endl;
    }
    
    if (_ConvBufferSize < _BufferSize)
        _ConvBufferSize = _BufferSize;
    
    _ConvBufferSize = nextPowerOfTwo(_ConvBufferSize);
    
    String debug;
    debug << "\ntrying to load " << configFile.getFullPathName() << "\n\n";
    
    DebugPrint(debug);
    
    // debug print samplerate and buffer size
    debug = "Samplerate: ";
    debug << _SampleRate;
    debug << " Host Buffer Size: ";
    debug << (int)_BufferSize;
    debug << " Internal Buffer Size: ";
    debug << (int)_ConvBufferSize;
    DebugPrint(debug);
    
    activePreset = configFile.getFileName(); // store filename only, on restart search preset folder for it!
    box_preset_str = configFile.getFileNameWithoutExtension();
    
    StringArray myLines;
    
    configFile.readLines(myLines);
    
    // global settings
    
    String directory("");
    
    AudioSampleBuffer TempAudioBuffer(1,256);
    
    conv_data.setSampleRate(_SampleRate);
    
    // iterate over all lines
    for (int currentLine = 0; currentLine < myLines.size(); currentLine++)
    {
        // get the line and remove spaces from start and end
        String line (myLines[currentLine].trim());
        
        if (line.startsWith("#"))
        {
            
            // ignore these lines
            
        } else if (line.contains("/cd")) {
            
            line = line.trimCharactersAtStart("/cd").trim();
            directory = line;
            
            std::cout << "Dir: " << directory << std::endl;
        
        } else if (line.contains("/convolver/new")) {
            int t_in_ch = 0;
            int t_out_ch = 0;
            
            line = line.trimCharactersAtStart("/convolver/new").trim();
            String::CharPointerType lineChar = line.getCharPointer();
            
            sscanf(lineChar, "%i%i", &t_in_ch, &t_out_ch);
            
            
        } else if (line.contains("/impulse/read"))
        {
            int in_ch = 0;
            int out_ch = 0;
            float gain = 1.f;
            int delay = 0;
            int offset = 0;
            int length = 0;
            int channel = 0;
            char filename[100];
            
            line = line.trimCharactersAtStart("/impulse/read").trim();
            
            String::CharPointerType lineChar = line.getCharPointer();
            
            
            sscanf(lineChar, "%i%i%f%i%i%i%i%s", &in_ch, &out_ch, &gain, &delay, &offset, &length, &channel, filename);
            
            // printf("load ir: %i %i %f %i %i %i %i %s \n", in_ch, out_ch, gain, delay, offset, length, channel, filename);
            
            File IrFilename;
            
            
            // check if /cd is defined in config
            if (directory.isEmpty()) {
                IrFilename = configFile.getParentDirectory().getChildFile(String(filename));
                
            } else { // /cd is defined
                if (File::isAbsolutePath(directory))
                {
                    // absolute path is defined
                    File path(directory);
                    
                    IrFilename = path.getChildFile(String(filename));
                } else {
                    
                    // relative path to the config file is defined
                    IrFilename = configFile.getParentDirectory().getChildFile(directory).getChildFile(String(filename));
                }
            }
            
            if ( ( in_ch < 1 ) || ( in_ch > getTotalNumInputChannels() ) || ( out_ch < 1 ) || ( out_ch > getTotalNumOutputChannels() ) )
            {
                
                String debug;
                debug << "ERROR: channel assignment not feasible: In: " << in_ch << " Out: " << out_ch;
                DebugPrint(debug << "\n");
                
                
            } else {
                
                double src_samplerate;
                if (loadIr(&TempAudioBuffer, IrFilename, channel-1, src_samplerate, gain, offset, length))
                {
                    // std::cout << "Length: " <<  TempAudioBuffer.getNumSamples() << " Channels: " << TempAudioBuffer.getNumChannels() << " MaxLevel: " << TempAudioBuffer.getRMSLevel(0, 0, 2048) << std::endl;
                    
                    // add IR to my convolution data - offset and length are already done while reading file, samplerate conversion is done during addIR
                    conv_data.addIR(in_ch-1, out_ch-1, 0, delay, 0, &TempAudioBuffer, 0, src_samplerate);
                    
                    String debug;
                    debug << "conv # " << conv_data.getNumIRs() << " " << IrFilename.getFullPathName() << " (" << TempAudioBuffer.getNumSamples() << " smpls) loaded";
                    DebugPrint(debug << "\n");
                    
                } else {
                    String debug;
                    debug << "ERROR: not loaded: " << IrFilename.getFullPathName();
                    DebugPrint(debug << "\n");
                    
                }
                
             
            } // end check channel assignment
            
            
        } // end "/impulse/read" line
        
        
        /*
        packed matrix in one .wav file as used in X-Volver by Angelo Farina
         
        one channel in the .wav file represents one output channel of the convolver matrix
        in one channel there are sequentially aligned input IRs
        
        the length of one impulse response is (length .wav) / #in_channels
         
        ir_#in_#out
         
        wav ch #1:  ir_1_1  ir_2_1  ir_3_1  ... ir_m_1
        wav ch #2:  ir_1_2  ir_2_2  ir_3_2  ... ir_m_2
        ...
        wav ch #3:  ir_1_n  ir_2_n  ir_3_n  ... ir_m_n
        
         
        by defining the number of input channels the plugin can recognize the length of each impulse response
        */
        else if (line.contains("/impulse/packedmatrix"))
        {
            int inchannels = 0;
            float gain = 1.f;
            int delay = 0;
            int offset = 0;
            int length = 0;
            char filename[100];
            
            line = line.trimCharactersAtStart("/impulse/packedmatrix").trim();
            
            String::CharPointerType lineChar = line.getCharPointer();
            
            
            sscanf(lineChar, "%i%f%i%i%i%s", &inchannels, &gain, &delay, &offset, &length, filename);
            
            File IrFilename;
            
            
            // check if /cd is defined in config
            if (directory.isEmpty()) {
                IrFilename = configFile.getParentDirectory().getChildFile(String(filename));
                
            } else { // /cd is defined
                if (File::isAbsolutePath(directory))
                {
                    // absolute path is defined
                    File path(directory);
                    
                    IrFilename = path.getChildFile(String(filename));
                } else {
                    
                    // relative path to the config file is defined
                    IrFilename = configFile.getParentDirectory().getChildFile(directory).getChildFile(String(filename));
                }
            }
            if (inchannels < 1)
            {
                String debug;
                debug << "ERROR: Number of input channels not feasible: " << inchannels;
                DebugPrint(debug << "\n");
                
                return;
            }
            
            
            double src_samplerate;
            if (loadIr(&TempAudioBuffer, IrFilename, -1, src_samplerate, gain, 0, 0)) // offset/length has to be done while processing individual irs
            {
                
                
                int numOutChannels = TempAudioBuffer.getNumChannels();
                int irLength = TempAudioBuffer.getNumSamples()/inchannels;
                
                if (irLength*inchannels != TempAudioBuffer.getNumSamples())
                {
                    String debug;
                    debug << "ERROR: length of wav file is not multiple of irLength*numinchannels!" << IrFilename.getFullPathName();
                    DebugPrint(debug << "\n");
                    
                    return;
                }
                
                if ((inchannels > getTotalNumInputChannels()) || numOutChannels > getTotalNumOutputChannels())
                {
                    String debug;
                    debug << "Input/Output channel assignement not feasible. Not enough input/output channels of the plugin. Need Input: " << inchannels << " Output: " << numOutChannels;
                    DebugPrint(debug << "\n");
                    
                    return;
                }
                
                if (length <= 0)
                    length = irLength-offset;
                
                // std::cout << "TotalLength: " <<  TempAudioBuffer.getNumSamples() << " IRLength: " <<  irLength << " Used Length: " << length << " Channels: " << TempAudioBuffer.getNumChannels() << std::endl;
                
                
                for (int i=0; i < numOutChannels; i++) {
                    for (int j=0; j < inchannels; j++) {
                        // add IR to my convolution data - offset and length are already done while reading file
                        int individualOffset = j*irLength;
                        
                        // std::cout << "AddIR: IN: " << j << " OUT: " << i << " individualOffset: " << individualOffset << std::endl;
                        
                        // CHECK IF LENGTH/OFFSET E.G. IS OK!!
                        conv_data.addIR(j, i, individualOffset + offset, delay, length, &TempAudioBuffer, i, src_samplerate);
                        
                    }
                }
                
                String debug;
                debug << "loaded " << conv_data.getNumIRs() << " filters with\nlength " << length << "\ninput channels: " << inchannels << "\noutput channels: " << numOutChannels << "\nfilename: " << IrFilename.getFileName();
                DebugPrint(debug << "\n\n");
                
            } else {
                String debug;
                debug << "ERROR: not loaded: " << IrFilename.getFullPathName();
                DebugPrint(debug << "\n");
                
                return;
            }
            
        } // end "/impulse/packedmatrix" line
        
        
    } // iterate over lines
    
    // initiate convolution
    
#ifdef USE_ZITA_CONVOLVER
    int err=0;
    
    unsigned int   options = 0;
    
    options |= Convproc::OPT_FFTW_MEASURE;
    options |= Convproc::OPT_VECTOR_MODE;
    
    zita_conv.set_options (options);
    zita_conv.set_density(0.5);
    
    printf("max length: %lli \n", conv_data.getMaxLength());
    
    err = zita_conv.configure(conv_data.getNumInputChannels(), conv_data.getNumOutputChannels(), (unsigned int)conv_data.getMaxLength(), _ConvBufferSize, _ConvBufferSize, Convproc::MAXPART);
    
    for (int i=0; i < conv_data.getNumIRs(); i++)
    {

        err = zita_conv.impdata_create(conv_data.getInCh(i), conv_data.getOutCh(i), 1, (float *)conv_data.getIR(i)->getReadPointer(0), (unsigned int)conv_data.getDelay(i), (unsigned int)conv_data.getLength(i));
        
    }
    
    zita_conv.print();
    zita_conv.start_process(CONVPROC_SCHEDULER_PRIORITY, CONVPROC_SCHEDULER_CLASS);
    
#else
    _MaxPartSize = jmin(MAX_PART_SIZE, nextPowerOfTwo(_MaxPartSize));
  
    mtxconv_.Configure(conv_data.getNumInputChannels(), conv_data.getNumOutputChannels(), _BufferSize, conv_data.getMaxLength(), _ConvBufferSize, _MaxPartSize);
    
    for (int i=0; i < conv_data.getNumIRs(); i++)
    {
        
        mtxconv_.AddFilter(conv_data.getInCh(i), conv_data.getOutCh(i), *conv_data.getIR(i));
        // no delay and length yet!
        
    }
    
    mtxconv_.StartProc();
    
#endif
    
    _configLoaded = true;
    
    setLatencySamples(_ConvBufferSize-_BufferSize);
    
    _skippedCycles.set(0);
    
    _min_in_ch = conv_data.getNumInputChannels();
    _min_out_ch = conv_data.getNumOutputChannels();
    _num_conv = conv_data.getNumIRs();
    
    _configFile = configFile;
    
    sendChangeMessage(); // notify editor
}

bool Mcfx_convolverAudioProcessor::loadIr(AudioSampleBuffer* IRBuffer, const File& audioFile, int channel, double &samplerate, float gain, int offset, int length)
{
    if (!audioFile.existsAsFile())
    {
        String debug;
        debug << "ERROR: file does not exist!";
        DebugPrint(debug << "\n");
        return false;
    }
    
    AudioFormatManager formatManager;
    
    // this can read .wav and .aiff
    formatManager.registerBasicFormats();
    
    AudioFormatReader* reader = formatManager.createReaderFor(audioFile);
    
    if (!reader) {
        String debug;
        debug << "ERROR: could not read impulse response file!";
        DebugPrint(debug << "\n");
        return false;
    }
    
	if (offset < 0)
        offset = 0;
    
    int64 wav_length = (int)reader->lengthInSamples;
    
    if (length <= 0)
        length = wav_length - offset;
    
    if (wav_length <= 0) {
        String debug;
        debug << "ERROR: zero samples in impulse response file!";
        DebugPrint(debug << "\n");
        return false;
    }
    
    if (wav_length-offset < length) {
        length = wav_length-offset;
        
        String debug;
        debug << "Warning: not enough samples in impulse response file, reading maximum number of samples: " << String(length);
        DebugPrint(debug << "\n");
    }
    
    if ((int)reader->numChannels <= channel) {
        String debug;
        debug << "ERROR: wav file doesn't have enough channels: " << String(reader->numChannels);
        DebugPrint(debug << "\n");
        return false;
    }
    
    
    AudioSampleBuffer ReadBuffer(reader->numChannels, length); // create buffer
    
    
    reader->read(&ReadBuffer, 0, length, offset, true, true);
    
    // set the samplerate -> maybe we have to resample later...
    samplerate = reader->sampleRate;
    
    // copy the wanted channel into our IR Buffer
    
    if (channel >= 0) // only one channel wanted...
    {
        IRBuffer->setSize(1, length);
        IRBuffer->copyFrom(0, 0, ReadBuffer, channel, 0, length);
    }
    else // copy all channels from the wav file if channel == -1 -> used for packedmatrix
    {
        IRBuffer->setSize(reader->numChannels, length);
        
        for (unsigned int i=0; i<reader->numChannels; i++) {
            IRBuffer->copyFrom(i, 0, ReadBuffer, i, 0, length);
        }
    }
   
    
    // scale ir with gain
    if (gain != 1.f)
        IRBuffer->applyGain(gain);
    
    // std::cout << "ReadRMS: " << IRBuffer->getRMSLevel(0, 0, ir_length) << std::endl;
    
    delete reader;
    
    
    return true;
}

unsigned int Mcfx_convolverAudioProcessor::getBufferSize()
{
    return _BufferSize;
}

unsigned int Mcfx_convolverAudioProcessor::getConvBufferSize()
{
    return _ConvBufferSize;
}

unsigned int Mcfx_convolverAudioProcessor::getMaxPartitionSize()
{
  return _MaxPartSize;
}

void Mcfx_convolverAudioProcessor::setConvBufferSize(unsigned int bufsize)
{
    if (nextPowerOfTwo(bufsize) != _ConvBufferSize)
    {
        _ConvBufferSize = nextPowerOfTwo(bufsize);
        ReloadConfiguration();
    }
}

void Mcfx_convolverAudioProcessor::setMaxPartitionSize(unsigned int maxsize)
{
    if (maxsize > MAX_PART_SIZE)
      return;
  
    if (nextPowerOfTwo(maxsize) != _MaxPartSize)
    {
      _MaxPartSize = nextPowerOfTwo(maxsize);
      ReloadConfiguration();
    }
}

void Mcfx_convolverAudioProcessor::UnloadConfiguration()
{
    // delete configuration
    _configLoaded = false;
    
    _conv_in.clear();
    _conv_out.clear();
    
    _min_in_ch = 0;
    _min_out_ch = 0;
    
#ifdef USE_ZITA_CONVOLVER
    
    zita_conv.stop_process();
    zita_conv.cleanup();

#else
    
    mtxconv_.StopProc();
    mtxconv_.Cleanup();
    
#endif
    
    conv_data.clear();
        
    std::cout << "Unloaded Convolution..." << std::endl;
    
}

void Mcfx_convolverAudioProcessor::DebugPrint(String debugText)
{
    String temp;
    
    temp << debugText;
    temp << _DebugText;
    
    // std::cout << debugText << std::endl;
    
    _DebugText = temp;
}

void Mcfx_convolverAudioProcessor::SearchPresets(File SearchFolder)
{
    _presetFiles.clear();
    
    SearchFolder.findChildFiles(_presetFiles, File::findFiles, true, "*.conf");
    std::cout << "Found preset files: " << _presetFiles.size() << std::endl;
    
}

void Mcfx_convolverAudioProcessor::LoadPreset(unsigned int preset)
{
    if (preset < (unsigned int)_presetFiles.size())
    {
        // ScheduleConfiguration(_presetFiles.getUnchecked(preset));
        LoadConfigurationAsync(_presetFiles.getUnchecked(preset));
    }
}

void Mcfx_convolverAudioProcessor::LoadPresetByName(String presetName)
{
    Array <File> files;
    presetDir.findChildFiles(files, File::findFiles, true, presetName);
    
    if (files.size())
    {
        LoadConfigurationAsync(files.getUnchecked(0)); // Load first result
        box_preset_str = files.getUnchecked(0).getFileNameWithoutExtension();
    }
    else
    { // preset not found -> post!
        String debug_msg;
        debug_msg << "ERROR loading preset: " << presetName << ", Preset not found in search folder!\n\n";
        DebugPrint(debug_msg);
    }
    
}

//==============================================================================
bool Mcfx_convolverAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* Mcfx_convolverAudioProcessor::createEditor()
{
    return new Mcfx_convolverAudioProcessorEditor (this);
}

//==============================================================================
void Mcfx_convolverAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    
    // Create an outer XML element..
    XmlElement xml ("MYPLUGINSETTINGS");
    
    // add some attributes to it..
    xml.setAttribute ("activePreset", activePreset);
    xml.setAttribute ("presetDir", presetDir.getFullPathName());
    xml.setAttribute("ConvBufferSize", (int)_ConvBufferSize);
    xml.setAttribute("MaxPartSize", (int)_MaxPartSize);
    // then use this helper function to stuff it into the binary blob and return it..
    copyXmlToBinary (xml, destData);
}

void Mcfx_convolverAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    
    ScopedPointer<XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    
    if (xmlState != nullptr)
    {
        String newPresetDir;
        
        // make sure that it's actually our type of XML object..
        if (xmlState->hasTagName ("MYPLUGINSETTINGS"))
        {
            // ok, now pull out our parameters..
            activePreset  = xmlState->getStringAttribute("activePreset", "");
            
            newPresetDir = xmlState->getStringAttribute("presetDir", presetDir.getFullPathName());
            
            _ConvBufferSize = xmlState->getIntAttribute("ConvBufferSize", _ConvBufferSize);
            _MaxPartSize = xmlState->getIntAttribute("MaxPartSize", _MaxPartSize);
        }
        
        File tempDir(newPresetDir);
        if (tempDir.exists()) {
            presetDir = tempDir;
            SearchPresets(presetDir);
        }
        
        if (activePreset.isNotEmpty()) {
            LoadPresetByName(activePreset);
        }
        
        
    }
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Mcfx_convolverAudioProcessor();
}
