/*
 * Carla Plugin Host
 * Copyright (C) 2011-2014 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the doc/GPL.txt file.
 */

#include "CarlaEngineGraph.hpp"
#include "CarlaEngineInternal.hpp"
#include "CarlaPlugin.hpp"

#include "CarlaMathUtils.hpp"
#include "CarlaMIDI.h"

#include "juce_audio_processors.h"
//#include "JuceInternalFilters.hpp"

using juce::jmax;
using juce::AudioPluginInstance;
using juce::FloatVectorOperations;
using juce::PluginDescription;

CARLA_BACKEND_START_NAMESPACE

// -----------------------------------------------------------------------
// Rack Graph stuff

static inline
uint getCarlaRackPortIdFromName(const char* const shortname) noexcept
{
    if (std::strcmp(shortname, "AudioIn1") == 0 || std::strcmp(shortname, "audio-in1") == 0)
        return RACK_GRAPH_CARLA_PORT_AUDIO_IN1;
    if (std::strcmp(shortname, "AudioIn2") == 0 || std::strcmp(shortname, "audio-in2") == 0)
        return RACK_GRAPH_CARLA_PORT_AUDIO_IN2;
    if (std::strcmp(shortname, "AudioOut1") == 0 || std::strcmp(shortname, "audio-out1") == 0)
        return RACK_GRAPH_CARLA_PORT_AUDIO_OUT1;
    if (std::strcmp(shortname, "AudioOut2") == 0 || std::strcmp(shortname, "audio-out2") == 0)
        return RACK_GRAPH_CARLA_PORT_AUDIO_OUT2;
    if (std::strcmp(shortname, "MidiIn") == 0 || std::strcmp(shortname, "midi-in") == 0)
        return RACK_GRAPH_CARLA_PORT_MIDI_IN;
    if (std::strcmp(shortname, "MidiOut") == 0 || std::strcmp(shortname, "midi-out") == 0)
        return RACK_GRAPH_CARLA_PORT_MIDI_OUT;

    carla_stderr("CarlaBackend::getCarlaRackPortIdFromName(%s) - invalid short name", shortname);
    return RACK_GRAPH_CARLA_PORT_NULL;
}

static inline
const char* getCarlaRackFullPortNameFromId(const /*RackGraphCarlaPortIds*/ uint portId)
{
    switch (portId)
    {
    case RACK_GRAPH_CARLA_PORT_AUDIO_IN1:
        return "Carla:AudioIn1";
    case RACK_GRAPH_CARLA_PORT_AUDIO_IN2:
        return "Carla:AudioIn2";
    case RACK_GRAPH_CARLA_PORT_AUDIO_OUT1:
        return "Carla:AudioOut1";
    case RACK_GRAPH_CARLA_PORT_AUDIO_OUT2:
        return "Carla:AudioOut2";
    case RACK_GRAPH_CARLA_PORT_MIDI_IN:
        return "Carla:MidiIn";
    case RACK_GRAPH_CARLA_PORT_MIDI_OUT:
        return "Carla:MidiOut";
    //case RACK_GRAPH_CARLA_PORT_NULL:
    //case RACK_GRAPH_CARLA_PORT_MAX:
    //    break;
    }

    carla_stderr("CarlaBackend::getCarlaRackFullPortNameFromId(%i) - invalid port id", portId);
    return nullptr;
}

// -----------------------------------------------------------------------
// RackGraph MIDI

const char* RackGraph::MIDI::getName(const bool isInput, const uint portId) const noexcept
{
    for (LinkedList<PortNameToId>::Itenerator it = isInput ? ins.begin() : outs.begin(); it.valid(); it.next())
    {
        const PortNameToId& port(it.getValue());

        if (port.port == portId)
            return port.name;
    }

    return nullptr;
}

uint RackGraph::MIDI::getPortId(const bool isInput, const char portName[], bool* const ok) const noexcept
{
    for (LinkedList<PortNameToId>::Itenerator it = isInput ? ins.begin() : outs.begin(); it.valid(); it.next())
    {
        const PortNameToId& port(it.getValue());

        if (std::strcmp(port.name, portName) == 0)
        {
            if (ok != nullptr)
                *ok = true;
            return port.port;
        }
    }

    if (ok != nullptr)
        *ok = false;
    return 0;
}

// -----------------------------------------------------------------------
// RackGraph

RackGraph::RackGraph(const uint32_t bufferSize, const uint32_t ins, const uint32_t outs) noexcept
    : inputs(ins),
      outputs(outs),
      isOffline(false)
{
    audio.inBuf[0]    = audio.inBuf[1]    = nullptr;
    audio.inBufTmp[0] = audio.inBufTmp[1] = nullptr;
    audio.outBuf[0]   = audio.outBuf[1]   = nullptr;

    setBufferSize(bufferSize);
}

RackGraph::~RackGraph() noexcept
{
    clearConnections();
}

void RackGraph::setBufferSize(const uint32_t bufferSize) noexcept
{
    if (audio.inBuf[0]    != nullptr) { delete[] audio.inBuf[0];    audio.inBuf[0]    = nullptr; }
    if (audio.inBuf[1]    != nullptr) { delete[] audio.inBuf[1];    audio.inBuf[1]    = nullptr; }
    if (audio.inBufTmp[0] != nullptr) { delete[] audio.inBufTmp[0]; audio.inBufTmp[0] = nullptr; }
    if (audio.inBufTmp[1] != nullptr) { delete[] audio.inBufTmp[1]; audio.inBufTmp[1] = nullptr; }
    if (audio.outBuf[0]   != nullptr) { delete[] audio.outBuf[0];   audio.outBuf[0]   = nullptr; }
    if (audio.outBuf[1]   != nullptr) { delete[] audio.outBuf[1];   audio.outBuf[1]   = nullptr; }

    try {
        audio.inBufTmp[0] = new float[bufferSize];
        audio.inBufTmp[1] = new float[bufferSize];

        if (inputs > 0 || outputs > 0)
        {
            audio.inBuf[0]    = new float[bufferSize];
            audio.inBuf[1]    = new float[bufferSize];
            audio.outBuf[0]   = new float[bufferSize];
            audio.outBuf[1]   = new float[bufferSize];
        }
    }
    catch(...) {
        if (audio.inBufTmp[0] != nullptr) { delete[] audio.inBufTmp[0]; audio.inBufTmp[0] = nullptr; }
        if (audio.inBufTmp[1] != nullptr) { delete[] audio.inBufTmp[1]; audio.inBufTmp[1] = nullptr; }

        if (inputs > 0 || outputs > 0)
        {
            if (audio.inBuf[0]  != nullptr) { delete[] audio.inBuf[0];  audio.inBuf[0]  = nullptr; }
            if (audio.inBuf[1]  != nullptr) { delete[] audio.inBuf[1];  audio.inBuf[1]  = nullptr; }
            if (audio.outBuf[0] != nullptr) { delete[] audio.outBuf[0]; audio.outBuf[0] = nullptr; }
            if (audio.outBuf[1] != nullptr) { delete[] audio.outBuf[1]; audio.outBuf[1] = nullptr; }
        }
        return;
    }

    FloatVectorOperations::clear(audio.inBufTmp[0], bufferSize);
    FloatVectorOperations::clear(audio.inBufTmp[1], bufferSize);

    if (inputs > 0 || outputs > 0)
    {
        FloatVectorOperations::clear(audio.inBuf[0],  bufferSize);
        FloatVectorOperations::clear(audio.inBuf[1],  bufferSize);
        FloatVectorOperations::clear(audio.outBuf[0], bufferSize);
        FloatVectorOperations::clear(audio.outBuf[1], bufferSize);
    }
}

void RackGraph::setOffline(const bool offline) noexcept
{
    isOffline = offline;
}

bool RackGraph::connect(CarlaEngine* const engine, const uint groupA, const uint portA, const uint groupB, const uint portB) noexcept
{
    CARLA_SAFE_ASSERT_RETURN(engine != nullptr, false);

    uint otherGroup, otherPort, carlaPort;

    if (groupA == RACK_GRAPH_GROUP_CARLA)
    {
        CARLA_SAFE_ASSERT_RETURN(groupB != RACK_GRAPH_GROUP_CARLA, false);

        carlaPort  = portA;
        otherGroup = groupB;
        otherPort  = portB;
    }
    else
    {
        CARLA_SAFE_ASSERT_RETURN(groupB == RACK_GRAPH_GROUP_CARLA, false);

        carlaPort  = portB;
        otherGroup = groupA;
        otherPort  = portA;
    }

    CARLA_SAFE_ASSERT_RETURN(carlaPort > RACK_GRAPH_CARLA_PORT_NULL && carlaPort < RACK_GRAPH_CARLA_PORT_MAX, false);
    CARLA_SAFE_ASSERT_RETURN(otherGroup > RACK_GRAPH_GROUP_CARLA && otherGroup < RACK_GRAPH_GROUP_MAX, false);

    bool makeConnection = false;

    switch (carlaPort)
    {
    case RACK_GRAPH_CARLA_PORT_AUDIO_IN1:
        CARLA_SAFE_ASSERT_RETURN(otherGroup == RACK_GRAPH_GROUP_AUDIO_IN, false);
        audio.mutex.lock();
        makeConnection = audio.connectedIn1.append(otherPort);
        audio.mutex.unlock();
        break;

    case RACK_GRAPH_CARLA_PORT_AUDIO_IN2:
        CARLA_SAFE_ASSERT_RETURN(otherGroup == RACK_GRAPH_GROUP_AUDIO_IN, false);
        audio.mutex.lock();
        makeConnection = audio.connectedIn2.append(otherPort);
        audio.mutex.unlock();
        break;

    case RACK_GRAPH_CARLA_PORT_AUDIO_OUT1:
        CARLA_SAFE_ASSERT_RETURN(otherGroup == RACK_GRAPH_GROUP_AUDIO_OUT, false);
        audio.mutex.lock();
        makeConnection = audio.connectedOut1.append(otherPort);
        audio.mutex.unlock();
        break;

    case RACK_GRAPH_CARLA_PORT_AUDIO_OUT2:
        CARLA_SAFE_ASSERT_RETURN(otherGroup == RACK_GRAPH_GROUP_AUDIO_OUT, false);
        audio.mutex.lock();
        makeConnection = audio.connectedOut2.append(otherPort);
        audio.mutex.unlock();
        break;

    case RACK_GRAPH_CARLA_PORT_MIDI_IN:
        CARLA_SAFE_ASSERT_RETURN(otherGroup == RACK_GRAPH_GROUP_MIDI_IN, false);
        if (const char* const portName = midi.getName(true, otherPort))
            makeConnection = engine->connectRackMidiInPort(portName);
        break;

    case RACK_GRAPH_CARLA_PORT_MIDI_OUT:
        CARLA_SAFE_ASSERT_RETURN(otherGroup == RACK_GRAPH_GROUP_MIDI_OUT, false);
        if (const char* const portName = midi.getName(false, otherPort))
            makeConnection = engine->connectRackMidiOutPort(portName);
        break;
    }

    if (! makeConnection)
    {
        engine->setLastError("Invalid rack connection");
        return false;
    }

    ConnectionToId connectionToId;
    connectionToId.setData(++connections.lastId, groupA, portA, groupB, portB);

    char strBuf[STR_MAX+1];
    strBuf[STR_MAX] = '\0';
    std::snprintf(strBuf, STR_MAX, "%u:%u:%u:%u", groupA, portA, groupB, portB);

    engine->callback(ENGINE_CALLBACK_PATCHBAY_CONNECTION_ADDED, connectionToId.id, 0, 0, 0.0f, strBuf);

    connections.list.append(connectionToId);
    return true;
}

bool RackGraph::disconnect(CarlaEngine* const engine, const uint connectionId) noexcept
{
    CARLA_SAFE_ASSERT_RETURN(engine != nullptr, false);
    CARLA_SAFE_ASSERT_RETURN(connections.list.count() > 0, false);

    for (LinkedList<ConnectionToId>::Itenerator it=connections.list.begin(); it.valid(); it.next())
    {
        const ConnectionToId& connection(it.getValue());

        if (connection.id != connectionId)
            continue;

        uint otherGroup, otherPort, carlaPort;

        if (connection.groupA == RACK_GRAPH_GROUP_CARLA)
        {
            CARLA_SAFE_ASSERT_RETURN(connection.groupB != RACK_GRAPH_GROUP_CARLA, false);

            carlaPort  = connection.portA;
            otherGroup = connection.groupB;
            otherPort  = connection.portB;
        }
        else
        {
            CARLA_SAFE_ASSERT_RETURN(connection.groupB == RACK_GRAPH_GROUP_CARLA, false);

            carlaPort  = connection.portB;
            otherGroup = connection.groupA;
            otherPort  = connection.portA;
        }

        CARLA_SAFE_ASSERT_RETURN(carlaPort > RACK_GRAPH_CARLA_PORT_NULL && carlaPort < RACK_GRAPH_CARLA_PORT_MAX, false);
        CARLA_SAFE_ASSERT_RETURN(otherGroup > RACK_GRAPH_GROUP_CARLA && otherGroup < RACK_GRAPH_GROUP_MAX, false);

        bool makeDisconnection = false;

        switch (carlaPort)
        {
        case RACK_GRAPH_CARLA_PORT_AUDIO_IN1:
            audio.mutex.lock();
            makeDisconnection = audio.connectedIn1.removeOne(otherPort);
            audio.mutex.unlock();
            break;

        case RACK_GRAPH_CARLA_PORT_AUDIO_IN2:
            audio.mutex.lock();
            makeDisconnection = audio.connectedIn2.removeOne(otherPort);
            audio.mutex.unlock();
            break;

        case RACK_GRAPH_CARLA_PORT_AUDIO_OUT1:
            audio.mutex.lock();
            makeDisconnection = audio.connectedOut1.removeOne(otherPort);
            audio.mutex.unlock();
            break;

        case RACK_GRAPH_CARLA_PORT_AUDIO_OUT2:
            audio.mutex.lock();
            makeDisconnection = audio.connectedOut2.removeOne(otherPort);
            audio.mutex.unlock();
            break;

        case RACK_GRAPH_CARLA_PORT_MIDI_IN:
            if (const char* const portName = midi.getName(true, otherPort))
                makeDisconnection = engine->disconnectRackMidiInPort(portName);
            break;

        case RACK_GRAPH_CARLA_PORT_MIDI_OUT:
            if (const char* const portName = midi.getName(false, otherPort))
                makeDisconnection = engine->disconnectRackMidiOutPort(portName);
            break;
        }

        if (! makeDisconnection)
        {
            engine->setLastError("Invalid rack connection");
            return false;
        }

        engine->callback(ENGINE_CALLBACK_PATCHBAY_CONNECTION_REMOVED, connection.id, 0, 0, 0.0f, nullptr);

        connections.list.remove(it);
        return true;
    }

    engine->setLastError("Failed to find connection");
    return false;
}

void RackGraph::clearConnections() noexcept
{
    connections.clear();

    audio.mutex.lock();
    audio.connectedIn1.clear();
    audio.connectedIn2.clear();
    audio.connectedOut1.clear();
    audio.connectedOut2.clear();
    audio.mutex.unlock();

    midi.ins.clear();
    midi.outs.clear();
}

const char* const* RackGraph::getConnections() noexcept
{
    if (connections.list.count() == 0)
        return nullptr;

    CarlaStringList connList;

    char strBuf[STR_MAX+1];
    strBuf[STR_MAX] = '\0';

    for (LinkedList<ConnectionToId>::Itenerator it=connections.list.begin(); it.valid(); it.next())
    {
        const ConnectionToId& connection(it.getValue());

        uint otherGroup, otherPort, carlaPort;

        if (connection.groupA == RACK_GRAPH_GROUP_CARLA)
        {
            CARLA_SAFE_ASSERT_CONTINUE(connection.groupB != RACK_GRAPH_GROUP_CARLA);

            carlaPort  = connection.portA;
            otherGroup = connection.groupB;
            otherPort  = connection.portB;
        }
        else
        {
            CARLA_SAFE_ASSERT_CONTINUE(connection.groupB == RACK_GRAPH_GROUP_CARLA);

            carlaPort  = connection.portB;
            otherGroup = connection.groupA;
            otherPort  = connection.portA;
        }

        CARLA_SAFE_ASSERT_CONTINUE(carlaPort > RACK_GRAPH_CARLA_PORT_NULL && carlaPort < RACK_GRAPH_CARLA_PORT_MAX);
        CARLA_SAFE_ASSERT_CONTINUE(otherGroup > RACK_GRAPH_GROUP_CARLA && otherGroup < RACK_GRAPH_GROUP_MAX);

        switch (carlaPort)
        {
        case RACK_GRAPH_CARLA_PORT_AUDIO_IN1:
        case RACK_GRAPH_CARLA_PORT_AUDIO_IN2:
            std::snprintf(strBuf, STR_MAX, "AudioIn:%i", otherPort+1);
            connList.append(strBuf);
            connList.append(getCarlaRackFullPortNameFromId(carlaPort));
            break;

        case RACK_GRAPH_CARLA_PORT_AUDIO_OUT1:
        case RACK_GRAPH_CARLA_PORT_AUDIO_OUT2:
            std::snprintf(strBuf, STR_MAX, "AudioOut:%i", otherPort+1);
            connList.append(getCarlaRackFullPortNameFromId(carlaPort));
            connList.append(strBuf);
            break;

        case RACK_GRAPH_CARLA_PORT_MIDI_IN:
            std::snprintf(strBuf, STR_MAX, "MidiIn:%s", midi.getName(true, otherPort));
            connList.append(strBuf);
            connList.append(getCarlaRackFullPortNameFromId(carlaPort));
            break;

        case RACK_GRAPH_CARLA_PORT_MIDI_OUT:
            std::snprintf(strBuf, STR_MAX, "MidiOut:%s", midi.getName(false, otherPort));
            connList.append(getCarlaRackFullPortNameFromId(carlaPort));
            connList.append(strBuf);
            break;
        }
    }

    if (connList.count() == 0)
        return nullptr;

    retCon = connList.toCharStringListPtr();

    return retCon;
}

bool RackGraph::getGroupAndPortIdFromFullName(const char* const fullPortName, uint& groupId, uint& portId) const noexcept
{
    CARLA_SAFE_ASSERT_RETURN(fullPortName != nullptr && fullPortName[0] != '\0', false);

    if (std::strncmp(fullPortName, "Carla:", 6) == 0)
    {
        groupId = RACK_GRAPH_GROUP_CARLA;
        portId  = getCarlaRackPortIdFromName(fullPortName+6);

        if (portId > RACK_GRAPH_CARLA_PORT_NULL && portId < RACK_GRAPH_CARLA_PORT_MAX)
            return true;
    }
    else if (std::strncmp(fullPortName, "AudioIn:", 8) == 0)
    {
        groupId = RACK_GRAPH_GROUP_AUDIO_IN;

        if (const int portTest = std::atoi(fullPortName+8))
        {
            portId = static_cast<uint>(portTest-1);
            return true;
        }
    }
    else if (std::strncmp(fullPortName, "AudioOut:", 9) == 0)
    {
        groupId = RACK_GRAPH_GROUP_AUDIO_OUT;

        if (const int portTest = std::atoi(fullPortName+9))
        {
            portId = static_cast<uint>(portTest-1);
            return true;
        }
    }
    else if (std::strncmp(fullPortName, "MidiIn:", 7) == 0)
    {
        groupId = RACK_GRAPH_GROUP_MIDI_IN;

        if (const char* const portName = fullPortName+7)
        {
            bool ok;
            portId = midi.getPortId(true, portName, &ok);
            return ok;
        }
    }
    else if (std::strncmp(fullPortName, "MidiOut:", 8) == 0)
    {
        groupId = RACK_GRAPH_GROUP_MIDI_OUT;

        if (const char* const portName = fullPortName+8)
        {
            bool ok;
            portId = midi.getPortId(false, portName, &ok);
            return ok;
        }
    }

    return false;
}

void RackGraph::process(CarlaEngine::ProtectedData* const data, const float* inBufReal[2], float* outBuf[2], const uint32_t frames)
{
    CARLA_SAFE_ASSERT_RETURN(data != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(data->events.in != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(data->events.out != nullptr,);

    // safe copy
    float inBuf0[frames];
    float inBuf1[frames];
    float* inBuf[2] = { inBuf0, inBuf1 };

    // initialize audio inputs
    FloatVectorOperations::copy(inBuf0, inBufReal[0], frames);
    FloatVectorOperations::copy(inBuf1, inBufReal[1], frames);

    // initialize audio outputs (zero)
    FloatVectorOperations::clear(outBuf[0], frames);
    FloatVectorOperations::clear(outBuf[1], frames);

    // initialize event outputs (zero)
    carla_zeroStruct<EngineEvent>(data->events.out, kMaxEngineEventInternalCount);

    bool processed = false;

    uint32_t oldAudioInCount = 0;
    uint32_t oldMidiOutCount = 0;

    // process plugins
    for (uint i=0; i < data->curPluginCount; ++i)
    {
        CarlaPlugin* const plugin = data->plugins[i].plugin;

        if (plugin == nullptr || ! plugin->isEnabled() || ! plugin->tryLock(isOffline))
            continue;

        if (processed)
        {
            // initialize audio inputs (from previous outputs)
            FloatVectorOperations::copy(inBuf0, outBuf[0], frames);
            FloatVectorOperations::copy(inBuf1, outBuf[1], frames);

            // initialize audio outputs (zero)
            FloatVectorOperations::clear(outBuf[0], frames);
            FloatVectorOperations::clear(outBuf[1], frames);

            // if plugin has no midi out, add previous events
            if (oldMidiOutCount == 0 && data->events.in[0].type != kEngineEventTypeNull)
            {
                if (data->events.out[0].type != kEngineEventTypeNull)
                {
                    // TODO: carefully add to input, sorted events
                }
                // else nothing needed
            }
            else
            {
                // initialize event inputs from previous outputs
                carla_copyStruct<EngineEvent>(data->events.in, data->events.out, kMaxEngineEventInternalCount);

                // initialize event outputs (zero)
                carla_zeroStruct<EngineEvent>(data->events.out, kMaxEngineEventInternalCount);
            }
        }

        oldAudioInCount = plugin->getAudioInCount();
        oldMidiOutCount = plugin->getMidiOutCount();

        // process
        plugin->initBuffers();
        plugin->process(inBuf, outBuf, frames);
        plugin->unlock();

        // if plugin has no audio inputs, add input buffer
        if (oldAudioInCount == 0)
        {
            FloatVectorOperations::add(outBuf[0], inBuf0, frames);
            FloatVectorOperations::add(outBuf[1], inBuf1, frames);
        }

        // set peaks
        {
            EnginePluginData& pluginData(data->plugins[i]);

            juce::Range<float> range;

            if (oldAudioInCount > 0)
            {
                range = FloatVectorOperations::findMinAndMax(inBuf0, static_cast<int>(frames));
                pluginData.insPeak[0] = carla_max<float>(std::abs(range.getStart()), std::abs(range.getEnd()), 1.0f);

                range = FloatVectorOperations::findMinAndMax(inBuf1, static_cast<int>(frames));
                pluginData.insPeak[1] = carla_max<float>(std::abs(range.getStart()), std::abs(range.getEnd()), 1.0f);
            }
            else
            {
                pluginData.insPeak[0] = 0.0f;
                pluginData.insPeak[1] = 0.0f;
            }

            if (plugin->getAudioOutCount() > 0)
            {
                range = FloatVectorOperations::findMinAndMax(outBuf[0], static_cast<int>(frames));
                pluginData.outsPeak[0] = carla_max<float>(std::abs(range.getStart()), std::abs(range.getEnd()), 1.0f);

                range = FloatVectorOperations::findMinAndMax(outBuf[1], static_cast<int>(frames));
                pluginData.outsPeak[1] = carla_max<float>(std::abs(range.getStart()), std::abs(range.getEnd()), 1.0f);
            }
            else
            {
                pluginData.outsPeak[0] = 0.0f;
                pluginData.outsPeak[1] = 0.0f;
            }
        }

        processed = true;
    }
}

void RackGraph::processHelper(CarlaEngine::ProtectedData* const data, const float* const* const inBuf, float* const* const outBuf, const uint32_t frames)
{
    CARLA_SAFE_ASSERT_RETURN(audio.outBuf[1] != nullptr,);

    const CarlaRecursiveMutexLocker _cml(audio.mutex);

    if (inBuf != nullptr && inputs > 0)
    {
        bool noConnections = true;

        // connect input buffers
        for (LinkedList<uint>::Itenerator it = audio.connectedIn1.begin(); it.valid(); it.next())
        {
            const uint& port(it.getValue());
            CARLA_SAFE_ASSERT_CONTINUE(port < inputs);

            if (noConnections)
            {
                FloatVectorOperations::copy(audio.inBuf[0], inBuf[port], frames);
                noConnections = false;
            }
            else
            {
                FloatVectorOperations::add(audio.inBuf[0], inBuf[port], frames);
            }
        }

        if (noConnections)
            FloatVectorOperations::clear(audio.inBuf[0], frames);

        noConnections = true;

        for (LinkedList<uint>::Itenerator it = audio.connectedIn2.begin(); it.valid(); it.next())
        {
            const uint& port(it.getValue());
            CARLA_SAFE_ASSERT_CONTINUE(port < inputs);

            if (noConnections)
            {
                FloatVectorOperations::copy(audio.inBuf[1], inBuf[port], frames);
                noConnections = false;
            }
            else
            {
                FloatVectorOperations::add(audio.inBuf[1], inBuf[port], frames);
            }
        }

        if (noConnections)
            FloatVectorOperations::clear(audio.inBuf[1], frames);
    }
    else
    {
        FloatVectorOperations::clear(audio.inBuf[0], frames);
        FloatVectorOperations::clear(audio.inBuf[1], frames);
    }

    FloatVectorOperations::clear(audio.outBuf[0], frames);
    FloatVectorOperations::clear(audio.outBuf[1], frames);

    // process
    process(data, const_cast<const float**>(audio.inBuf), audio.outBuf, frames);

    // connect output buffers
    if (audio.connectedOut1.count() != 0)
    {
        for (LinkedList<uint>::Itenerator it = audio.connectedOut1.begin(); it.valid(); it.next())
        {
            const uint& port(it.getValue());
            CARLA_SAFE_ASSERT_CONTINUE(port < outputs);

            FloatVectorOperations::add(outBuf[port], audio.outBuf[0], frames);
        }
    }

    if (audio.connectedOut2.count() != 0)
    {
        for (LinkedList<uint>::Itenerator it = audio.connectedOut2.begin(); it.valid(); it.next())
        {
            const uint& port(it.getValue());
            CARLA_SAFE_ASSERT_CONTINUE(port < outputs);

            FloatVectorOperations::add(outBuf[port], audio.outBuf[1], frames);
        }
    }
}

// -----------------------------------------------------------------------

class CarlaPluginInstance : public AudioPluginInstance
{
public:
    CarlaPluginInstance(CarlaPlugin* const plugin)
        : fPlugin(plugin),
          fGraph(nullptr) {}

    ~CarlaPluginInstance() override
    {
    }

    AudioProcessorGraph* getParentGraph() const noexcept
    {
        return fGraph;
    }

    void setParentGraph(AudioProcessorGraph*)
    {
    }

    void* getPlatformSpecificData() noexcept override
    {
        return fPlugin;
    }

    void fillInPluginDescription(PluginDescription& d) const override
    {
        d.pluginFormatName = "Carla";
        d.category = "Carla Plugin";
        d.version = "1.0";

        CARLA_SAFE_ASSERT_RETURN(fPlugin != nullptr,);

        char strBuf[STR_MAX+1];
        strBuf[STR_MAX] = '\0';

        fPlugin->getRealName(strBuf);
        d.name = strBuf;

        fPlugin->getLabel(strBuf);
        d.descriptiveName = strBuf;

        fPlugin->getMaker(strBuf);
        d.manufacturerName = strBuf;

        d.uid = d.name.hashCode();
        d.isInstrument = (fPlugin->getHints() & PLUGIN_IS_SYNTH);

        d.numInputChannels  = fPlugin->getAudioInCount();
        d.numOutputChannels = fPlugin->getAudioOutCount();
        //d.hasSharedContainer = true;
    }

private:
    CarlaPlugin* const fPlugin;
    AudioProcessorGraph* fGraph;
};

// -----------------------------------------------------------------------
// PatchbayGraph

// TODO

PatchbayGraph::PatchbayGraph(const uint32_t bufferSize, const double sampleRate, const uint32_t ins, const uint32_t outs)
    : inputs(ins),
      outputs(outs)
{
    graph.setPlayConfigDetails(inputs, outputs, sampleRate, bufferSize);
    graph.prepareToPlay(sampleRate, bufferSize);
    audioBuffer.setSize(jmax(ins, outs), bufferSize);
    midiBuffer.ensureSize(kMaxEngineEventInternalCount*2);
    midiBuffer.clear();

    for (uint32_t i=0; i<ins; ++i)
    {
        AudioProcessorGraph::AudioGraphIOProcessor* const proc(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
        graph.addNode(proc);
    }

    for (uint32_t i=0; i<outs; ++i)
    {
        AudioProcessorGraph::AudioGraphIOProcessor* const proc(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
        graph.addNode(proc);
    }
}

PatchbayGraph::~PatchbayGraph()
{
    graph.releaseResources();
    graph.clear();
    audioBuffer.clear();
}

void PatchbayGraph::setBufferSize(const uint32_t bufferSize)
{
    graph.releaseResources();
    graph.prepareToPlay(graph.getSampleRate(), bufferSize);
    audioBuffer.setSize(audioBuffer.getNumChannels(), bufferSize);
}

void PatchbayGraph::setSampleRate(const double sampleRate)
{
    graph.releaseResources();
    graph.prepareToPlay(sampleRate, graph.getBlockSize());
}

void PatchbayGraph::setOffline(const bool offline)
{
    graph.setNonRealtime(offline);
}

#if 0
bool PatchbayGraph::connect(CarlaEngine* const engine, const uint /*groupA*/, const uint /*portA*/, const uint /*groupB*/, const uint /*portB*/) noexcept
{
    CARLA_SAFE_ASSERT_RETURN(engine != nullptr, false);

    return false;
}

bool PatchbayGraph::disconnect(CarlaEngine* const engine, const uint /*connectionId*/) noexcept
{
    CARLA_SAFE_ASSERT_RETURN(engine != nullptr, false);

    return false;
}

const char* const* PatchbayGraph::getConnections() const noexcept
{
    return nullptr;
}

bool PatchbayGraph::getPortIdFromFullName(const char* const /*fillPortName*/, uint& /*groupId*/, uint& /*portId*/) const noexcept
{
    return false;
}
#endif

void PatchbayGraph::process(CarlaEngine::ProtectedData* const data, const float* const* const inBuf, float* const* const outBuf, const uint32_t frames)
{
    CARLA_SAFE_ASSERT_RETURN(data != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(data->events.in != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(data->events.out != nullptr,);

    // put events in juce buffer
    {
        uint8_t        size     = 0;
        uint8_t        mdata[3] = { 0, 0, 0 };
        const uint8_t* mdataPtr = mdata;

        midiBuffer.clear();

        for (ushort i=0; i < kMaxEngineEventInternalCount; ++i)
        {
            const EngineEvent& engineEvent(data->events.in[i]);

            if (engineEvent.type == kEngineEventTypeNull)
                break;

            else if (engineEvent.type == kEngineEventTypeControl)
            {
                const EngineControlEvent& ctrlEvent(engineEvent.ctrl);
                ctrlEvent.convertToMidiData(engineEvent.channel, size, mdata);
                mdataPtr = mdata;
            }
            else if (engineEvent.type == kEngineEventTypeMidi)
            {
                const EngineMidiEvent& midiEvent(engineEvent.midi);

                size = midiEvent.size;

                if (size > EngineMidiEvent::kDataSize && midiEvent.dataExt != nullptr)
                    mdataPtr = midiEvent.dataExt;
                else
                    mdataPtr = midiEvent.data;
            }
            else
            {
                continue;
            }

            if (size > 0)
                midiBuffer.addEvent(mdataPtr, size, engineEvent.time);
        }
    }

    // initialize event outputs (zero)
    carla_zeroStruct<EngineEvent>(data->events.out, kMaxEngineEventInternalCount);

    // put carla audio in juce buffer
    {
        int i=0;

        for (; i<inputs; ++i)
            FloatVectorOperations::copy(audioBuffer.getWritePointer(i), inBuf[i], frames);

        // clear remaining channels
        for (int count=audioBuffer.getNumChannels(); i<count; ++i)
            audioBuffer.clear(i, 0, frames);
    }

    graph.processBlock(audioBuffer, midiBuffer);

    // put juce audio in carla buffer
    {
        for (int i=0; i<outputs; ++i)
            FloatVectorOperations::copy(outBuf[i], audioBuffer.getReadPointer(i), frames);
    }

    // put juce events in carla buffer
    {
        const uint8_t* midiData;
        int numBytes;
        int sampleNumber;
        ushort engineEventIndex = 0;

        for (MidiBuffer::Iterator outBufferIterator(midiBuffer); outBufferIterator.getNextEvent(midiData, numBytes, sampleNumber);)
        {
            if (numBytes <= 0 || numBytes >= MAX_MIDI_VALUE)
                continue;

            EngineEvent& engineEvent(data->events.out[engineEventIndex++]);

            engineEvent.time = sampleNumber;
            engineEvent.fillFromMidiData(static_cast<uint8_t>(numBytes), midiData);

            if (engineEventIndex >= kMaxEngineEventInternalCount)
                break;
        }
    }
}

// -----------------------------------------------------------------------
// InternalGraph

EngineInternalGraph::EngineInternalGraph() noexcept
    : fIsRack(true),
      fIsReady(false)
{
    fRack = nullptr;
}

EngineInternalGraph::~EngineInternalGraph() noexcept
{
    CARLA_SAFE_ASSERT(! fIsReady);
    CARLA_SAFE_ASSERT(fRack == nullptr);
}

void EngineInternalGraph::create(const bool isRack, const double sampleRate, const uint32_t bufferSize, const uint32_t inputs, const uint32_t outputs)
{
    fIsRack = isRack;

    if (isRack)
    {
        CARLA_SAFE_ASSERT_RETURN(fRack == nullptr,);
        fRack = new RackGraph(bufferSize, inputs, outputs);
    }
    else
    {
        CARLA_SAFE_ASSERT_RETURN(fPatchbay == nullptr,);
        fPatchbay = new PatchbayGraph(sampleRate, bufferSize, inputs, outputs);
    }

    fIsReady = true;
}

void EngineInternalGraph::destroy() noexcept
{
    if (! fIsReady)
    {
        CARLA_SAFE_ASSERT(fRack == nullptr);
        return;
    }

    fIsReady = false;

    if (fIsRack)
    {
        CARLA_SAFE_ASSERT_RETURN(fRack != nullptr,);
        delete fRack;
        fRack = nullptr;
    }
    else
    {
        CARLA_SAFE_ASSERT_RETURN(fPatchbay != nullptr,);
        delete fPatchbay;
        fPatchbay = nullptr;
    }
}

void EngineInternalGraph::setBufferSize(const uint32_t bufferSize)
{
    ScopedValueSetter<bool> svs(fIsReady, false, true);

    if (fIsRack)
    {
        CARLA_SAFE_ASSERT_RETURN(fRack != nullptr,);
        fRack->setBufferSize(bufferSize);
    }
    else
    {
        CARLA_SAFE_ASSERT_RETURN(fPatchbay != nullptr,);
        fPatchbay->setBufferSize(bufferSize);
    }
}

void EngineInternalGraph::setSampleRate(const double sampleRate)
{
    ScopedValueSetter<bool> svs(fIsReady, false, true);

    if (fIsRack)
    {
        CARLA_SAFE_ASSERT_RETURN(fRack != nullptr,);
    }
    else
    {
        CARLA_SAFE_ASSERT_RETURN(fPatchbay != nullptr,);
        fPatchbay->setSampleRate(sampleRate);
    }
}

void EngineInternalGraph::setOffline(const bool offline)
{
    ScopedValueSetter<bool> svs(fIsReady, false, true);

    if (fIsRack)
    {
        CARLA_SAFE_ASSERT_RETURN(fRack != nullptr,);
        fRack->setOffline(offline);
    }
    else
    {
        CARLA_SAFE_ASSERT_RETURN(fPatchbay != nullptr,);
        fPatchbay->setOffline(offline);
    }
}

bool EngineInternalGraph::isReady() const noexcept
{
    return fIsReady;
}

RackGraph* EngineInternalGraph::getRackGraph() const noexcept
{
    CARLA_SAFE_ASSERT_RETURN(fIsRack, nullptr);

    return fRack;
}

PatchbayGraph* EngineInternalGraph::getPatchbayGraph() const noexcept
{
    CARLA_SAFE_ASSERT_RETURN(! fIsRack, nullptr);

    return fPatchbay;
}

void EngineInternalGraph::process(CarlaEngine::ProtectedData* const data, const float* const* const inBuf, float* const* const outBuf, const uint32_t frames)
{
    if (fIsRack)
    {
        CARLA_SAFE_ASSERT_RETURN(fRack != nullptr,);
        fRack->processHelper(data, inBuf, outBuf, frames);
    }
    else
    {
        CARLA_SAFE_ASSERT_RETURN(fPatchbay != nullptr,);
        fPatchbay->process(data, inBuf, outBuf, frames);
    }
}

void EngineInternalGraph::processRack(CarlaEngine::ProtectedData* const data, const float* inBuf[2], float* outBuf[2], const uint32_t frames)
{
    CARLA_SAFE_ASSERT_RETURN(fIsRack,);
    CARLA_SAFE_ASSERT_RETURN(fRack != nullptr,);

    fRack->process(data, inBuf, outBuf, frames);
}

// -----------------------------------------------------------------------
// CarlaEngine Patchbay stuff

bool CarlaEngine::patchbayConnect(const uint groupA, const uint portA, const uint groupB, const uint portB)
{
    CARLA_SAFE_ASSERT_RETURN(pData->options.processMode == ENGINE_PROCESS_MODE_CONTINUOUS_RACK || pData->options.processMode == ENGINE_PROCESS_MODE_PATCHBAY, false);
    //CARLA_SAFE_ASSERT_RETURN(pData->graph.isReady, false);
    //CARLA_SAFE_ASSERT_RETURN(pData->graph.graph != nullptr, false);
    carla_stdout("CarlaEngine::patchbayConnect(%u, %u, %u, %u)", groupA, portA, groupB, portB);

    return false; //pData->graph.graph->connect(this, groupA, portA, groupB, portB);
}

bool CarlaEngine::patchbayDisconnect(const uint connectionId)
{
    CARLA_SAFE_ASSERT_RETURN(pData->options.processMode == ENGINE_PROCESS_MODE_CONTINUOUS_RACK || pData->options.processMode == ENGINE_PROCESS_MODE_PATCHBAY, false);
    //CARLA_SAFE_ASSERT_RETURN(pData->graph.isReady, false);
    //CARLA_SAFE_ASSERT_RETURN(pData->graph.graph != nullptr, false);
    carla_stdout("CarlaEngine::patchbayDisconnect(%u)", connectionId);

    return false; //pData->graph.graph->disconnect(this, connectionId);
}

bool CarlaEngine::patchbayRefresh()
{
    setLastError("Unsupported operation");
    return false;
}

// -----------------------------------------------------------------------

const char* const* CarlaEngine::getPatchbayConnections() const
{
    //CARLA_SAFE_ASSERT_RETURN(pData->graph.isReady, nullptr);
    //CARLA_SAFE_ASSERT_RETURN(pData->graph.graph != nullptr, nullptr);
    carla_debug("CarlaEngine::getPatchbayConnections()");

    return nullptr; //pData->graph.graph->getConnections();
}

void CarlaEngine::restorePatchbayConnection(const char* const connSource, const char* const connTarget)
{
    //CARLA_SAFE_ASSERT_RETURN(pData->graph.isReady,);
    //CARLA_SAFE_ASSERT_RETURN(pData->graph.graph != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(connSource != nullptr && connSource[0] != '\0',);
    CARLA_SAFE_ASSERT_RETURN(connTarget != nullptr && connTarget[0] != '\0',);
    carla_debug("CarlaEngine::restorePatchbayConnection(\"%s\", \"%s\")", connSource, connTarget);

//     uint groupA, portA;
//     uint groupB, portB;
//
//     if (! pData->graph.graph->getPortIdFromFullName(connSource, groupA, portA))
//         return;
//     if (! pData->graph.graph->getPortIdFromFullName(connTarget, groupB, portB))
//         return;
//
//     patchbayConnect(groupA, portA, groupB, portB);
}

// -----------------------------------------------------------------------

CARLA_BACKEND_END_NAMESPACE
