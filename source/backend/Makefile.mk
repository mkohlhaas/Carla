#!/usr/bin/make -f
# Makefile for carla-backend #
# -------------------------- #
# Created by falkTX
#

include ../../Makefile.mk

# --------------------------------------------------------------

BUILD_CXX_FLAGS += -I. -I.. -I../../includes -I../../utils -isystem ../../modules

BUILD_CXX_FLAGS += $(LIBLO_FLAGS)
BUILD_CXX_FLAGS += $(QTCORE_FLAGS)
BUILD_CXX_FLAGS += $(QTXML_FLAGS)

# --------------------------------------------------------------

BUILD_CXX_FLAGS += -DWANT_NATIVE

ifeq ($(CARLA_PLUGIN_SUPPORT),true)
BUILD_CXX_FLAGS += -DWANT_LADSPA -DWANT_DSSI -DWANT_LV2
# -DWANT_VST
# ifeq ($(CARLA_VESTIGE_HEADER),true)
# BUILD_CXX_FLAGS += -DVESTIGE_HEADER
# endif
endif

# --------------------------------------------------------------

# ifeq ($(HAVE_CSOUND),true)
# BUILD_CXX_FLAGS += -DWANT_CSOUND
# endif

ifeq ($(HAVE_FLUIDSYNTH),true)
BUILD_CXX_FLAGS += -DWANT_FLUIDSYNTH
endif

ifeq ($(HAVE_LINUXSAMPLER),true)
BUILD_CXX_FLAGS += -DWANT_LINUXSAMPLER
endif

# --------------------------------------------------------------

ifeq ($(HAVE_AF_DEPS),true)
BUILD_CXX_FLAGS += -DWANT_AUDIOFILE
endif

ifeq ($(HAVE_MF_DEPS),true)
BUILD_CXX_FLAGS += -DWANT_MIDIFILE
endif

ifeq ($(HAVE_ZYN_DEPS),true)
BUILD_CXX_FLAGS += -DWANT_ZYNADDSUBFX
ifeq ($(HAVE_ZYN_UI_DEPS),true)
BUILD_CXX_FLAGS += -DWANT_ZYNADDSUBFX_UI
endif
endif

# --------------------------------------------------------------

CARLA_BACKEND_H  = ../CarlaBackend.h $(CARLA_DEFINES_H)
CARLA_HOST_H     = ../CarlaHost.h $(CARLA_BACKEND_H)
CARLA_ENGINE_HPP = ../CarlaEngine.hpp $(CARLA_BACKEND_H)
CARLA_PLUGIN_HPP = ../CarlaPlugin.hpp $(CARLA_BACKEND_H)

CARLA_DEFINES_H = ../../includes/CarlaDefines.h
CARLA_MIDI_H    = ../../includes/CarlaMIDI.h

CARLA_UTILS_HPP         = ../../utils/CarlaUtils.hpp $(CARLA_DEFINES_H)
CARLA_BACKEND_UTILS_HPP = ../../utils/CarlaBackendUtils.hpp $(CARLA_BACKEND_H) $(CARLA_HOST_H) $(CARLA_STRING_HPP)
CARLA_ENGINE_UTILS_HPP  = ../../utils/CarlaEngineUtils.hpp $(CARLA_ENGINE_HPP) $(CARLA_UTILS_HPP)
CARLA_JUCE_UTILS_HPP    = ../../utils/CarlaJuceUtils.hpp $(CARLA_UTILS_HPP)
CARLA_STATE_UTILS_HPP   = ../../utils/CarlaStateUtils.hpp $(CARLA_BACKEND_UTILS_HPP) $(CARLA_MIDI_H) $(LINKED_LIST_HPP)

CARLA_MUTEX_HPP      = ../../utils/CarlaMutex.hpp $(CARLA_UTILS_HPP)
CARLA_STRING_HPP     = ../../utils/CarlaString.hpp $(CARLA_JUCE_UTILS_HPP)
CARLA_THREAD_HPP     = ../../utils/CarlaThread.hpp $(CARLA_MUTEX_HPP) $(CARLA_STRING_HPP)

LINKED_LIST_HPP      = ../../utils/LinkedList.hpp $(CARLA_UTILS_HPP)
