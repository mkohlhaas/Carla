/*
  ZynAddSubFX - a software synthesizer

  Alienwah.cpp - "AlienWah" effect
  Copyright (C) 2002-2005 Nasca Octavian Paul
  Author: Nasca Octavian Paul

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/

#include <cmath>
#include <rtosc/port-sugar.h>
#include <rtosc/ports.h>
#include "../Misc/Allocator.h"
#include "Alienwah.h"

using std::complex;

#define rObject Alienwah
#define rBegin [](const char *, rtosc::RtData &) {
#define rEnd }

rtosc::Ports Alienwah::ports = {
    {"preset::i", rOptions(Alienwah 1, Alienwah 2, Alienwah 3, Alienwah 4)
                  rDoc("Instrument Presets"), 0,
                  rBegin;
                  rEnd},
    //Pvolume/Ppanning are common
    rEffPar(Pfreq,     2, rShort("freq"), "Effect Frequency"),
    rEffPar(Pfreqrnd,  3, rShort("rand"), "Frequency Randomness"),
    rEffPar(PLFOtype,  4, rShort("shape"), "LFO Shape"),
    rEffParTF(PStereo, 5, rShort("stereo"), "Stereo/Mono Mode"),
    rEffPar(Pdepth,    6, rShort("depth"), "LFO Depth"),
    rEffPar(Pfeedback, 7, rShort("fb"), "Feedback"),
    rEffPar(Pdelay,    8, rShort("delay"), "Delay"),
    rEffPar(Plrcross,  9, rShort("l/r"), "Left/Right Crossover"),
    rEffPar(Pphase,   10, rShort("phase"), "Phase"),
};
#undef rBegin
#undef rEnd
#undef rObject

Alienwah::Alienwah(EffectParams pars)
    :Effect(pars),
      lfo(pars.srate, pars.bufsize),
      oldl(NULL),
      oldr(NULL)
{
    setpreset(Ppreset);
    cleanup();
    oldclfol = complex<float>(fb, 0.0f);
    oldclfor = complex<float>(fb, 0.0f);
}

Alienwah::~Alienwah()
{
    memory.devalloc(oldl);
    memory.devalloc(oldr);
}


//Apply the effect
void Alienwah::out(const Stereo<float *> &smp)
{
    float lfol, lfor; //Left/Right LFOs
    complex<float> clfol, clfor;
    /**\todo Rework, as optimization can be used when the new complex type is
     * utilized.
     * Before all calculations needed to be done with individual float,
     * but now they can be done together*/
    lfo.effectlfoout(&lfol, &lfor);
    lfol *= depth * PI * 2.0f;
    lfor *= depth * PI * 2.0f;
    clfol = complex<float>(cosf(lfol + phase) * fb, sinf(lfol + phase) * fb); //rework
    clfor = complex<float>(cosf(lfor + phase) * fb, sinf(lfor + phase) * fb); //rework

    for(int i = 0; i < buffersize; ++i) {
        float x  = ((float) i) / buffersize_f;
        float x1 = 1.0f - x;
        //left
        complex<float> tmp = clfol * x + oldclfol * x1;

        complex<float> out = tmp * oldl[oldk];
        out += (1 - fabs(fb)) * smp.l[i] * pangainL;

        oldl[oldk] = out;
        float l = out.real() * 10.0f * (fb + 0.1f);

        //right
        tmp = clfor * x + oldclfor * x1;

        out = tmp * oldr[oldk];
        out += (1 - fabs(fb)) * smp.r[i] * pangainR;

        oldr[oldk] = out;
        float r = out.real() * 10.0f * (fb + 0.1f);


        if(++oldk >= Pdelay)
            oldk = 0;
        //LRcross
        efxoutl[i] = l * (1.0f - lrcross) + r * lrcross;
        efxoutr[i] = r * (1.0f - lrcross) + l * lrcross;
    }

    oldclfol = clfol;
    oldclfor = clfor;
}

//Cleanup the effect
void Alienwah::cleanup(void)
{
    for(int i = 0; i < Pdelay; ++i) {
        oldl[i] = complex<float>(0.0f, 0.0f);
        oldr[i] = complex<float>(0.0f, 0.0f);
    }
    oldk = 0;
}


//Parameter control
void Alienwah::setdepth(unsigned char _Pdepth)
{
    Pdepth = _Pdepth;
    depth  = Pdepth / 127.0f;
}

void Alienwah::setfb(unsigned char _Pfb)
{
    Pfb = _Pfb;
    fb  = fabs((Pfb - 64.0f) / 64.1f);
    fb  = sqrtf(fb);
    if(fb < 0.4f)
        fb = 0.4f;
    if(Pfb < 64)
        fb = -fb;
}

void Alienwah::setvolume(unsigned char _Pvolume)
{
    Pvolume   = _Pvolume;
    outvolume = Pvolume / 127.0f;
    if(insertion == 0)
        volume = 1.0f;
    else
        volume = outvolume;
}

void Alienwah::setphase(unsigned char _Pphase)
{
    Pphase = _Pphase;
    phase  = (Pphase - 64.0f) / 64.0f * PI;
}

void Alienwah::setdelay(unsigned char _Pdelay)
{
    memory.devalloc(oldl);
    memory.devalloc(oldr);
    Pdelay = (_Pdelay >= MAX_ALIENWAH_DELAY) ? MAX_ALIENWAH_DELAY : _Pdelay;
    oldl   = memory.valloc<complex<float>>(Pdelay);
    oldr   = memory.valloc<complex<float>>(Pdelay);
    cleanup();
}

void Alienwah::setpreset(unsigned char npreset)
{
    const int     PRESET_SIZE = 11;
    const int     NUM_PRESETS = 4;
    unsigned char presets[NUM_PRESETS][PRESET_SIZE] = {
        //AlienWah1
        {127, 64, 70, 0,   0, 62,  60,  105, 25, 0, 64},
        //AlienWah2
        {127, 64, 73, 106, 0, 101, 60,  105, 17, 0, 64},
        //AlienWah3
        {127, 64, 63, 0,   1, 100, 112, 105, 31, 0, 42},
        //AlienWah4
        {93,  64, 25, 0,   1, 66,  101, 11,  47, 0, 86}
    };

    if(npreset >= NUM_PRESETS)
        npreset = NUM_PRESETS - 1;
    for(int n = 0; n < PRESET_SIZE; ++n)
        changepar(n, presets[npreset][n]);
    if(insertion == 0)
        changepar(0, presets[npreset][0] / 2);  //lower the volume if this is system effect
    Ppreset = npreset;
}


void Alienwah::changepar(int npar, unsigned char value)
{
    switch(npar) {
        case 0:
            setvolume(value);
            break;
        case 1:
            setpanning(value);
            break;
        case 2:
            lfo.Pfreq = value;
            lfo.updateparams();
            break;
        case 3:
            lfo.Prandomness = value;
            lfo.updateparams();
            break;
        case 4:
            lfo.PLFOtype = value;
            lfo.updateparams();
            break;
        case 5:
            lfo.Pstereo = value;
            lfo.updateparams();
            break;
        case 6:
            setdepth(value);
            break;
        case 7:
            setfb(value);
            break;
        case 8:
            setdelay(value);
            break;
        case 9:
            setlrcross(value);
            break;
        case 10:
            setphase(value);
            break;
    }
}

unsigned char Alienwah::getpar(int npar) const
{
    switch(npar) {
        case 0:  return Pvolume;
        case 1:  return Ppanning;
        case 2:  return lfo.Pfreq;
        case 3:  return lfo.Prandomness;
        case 4:  return lfo.PLFOtype;
        case 5:  return lfo.Pstereo;
        case 6:  return Pdepth;
        case 7:  return Pfb;
        case 8:  return Pdelay;
        case 9:  return Plrcross;
        case 10: return Pphase;
        default: return 0;
    }
}
