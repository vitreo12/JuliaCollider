#include "SC_PlugIn.h"

/*******************************************************
    SCBuffer.jl functions. Declared in julia.h 
    and defined here. JL_DLLEXPORT macro in julia.h, 
    simply sets __attribute__((visibility("default"))).
    Here it's just been done manually. 
    Otherwise, the functions would have been compiled
    as local (with command nm, they would show as "t" 
    instead of "T")
*******************************************************/

extern "C" 
{
    __attribute__((visibility("default"))) void* jl_get_buf_shared_SC(void* buffer_SCWorld, float fbufnum)
    {
        printf("*** NEW BUFFER!!! ***\n");
        World* SCWorld = (World*)buffer_SCWorld;

        uint32 bufnum = (int)fbufnum; 

        //If bufnum is not more that maximum number of buffers in World* it means bufnum doesn't point to a LocalBuf
        if(!(bufnum >= SCWorld->mNumSndBufs))
        {
            SndBuf* buf = SCWorld->mSndBufs + bufnum; 

            if(!buf->data)
            {
                printf("WARNING: Julia: Invalid buffer: %d\n", bufnum);
                return nullptr;
            }

            /* THIS MACRO IS USELESS HERE FOR SUPERNOVA. It should be set after each call to jl_get_buf_shared_SC to lock
            the buffer for the entirety of the Julia function... */
            LOCK_SNDBUF_SHARED(buf); 

            return (void*)buf;
        }
        else
        {
            printf("WARNING: Julia: local buffers are not yet supported \n");
            
            return nullptr;
        
            /* int localBufNum = bufnum - SCWorld->mNumSndBufs; 
            
            Graph *parent = unit->mParent; 
            
            if(localBufNum <= parent->localBufNum)
                unit->m_buf = parent->mLocalSndBufs + localBufNum; 
            else 
            { 
                bufnum = 0; 
                unit->m_buf = SCWorld->mSndBufs + bufnum; 
            } 

            return (void*)buf;
            */
        }
    }

    __attribute__((visibility("default"))) float jl_get_float_value_buf_SC(void* buf, size_t index, size_t channel)
    {
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;
            
            size_t c_index = index - 1; //Julia counts from 1, that's why index - 1
            
            size_t actual_index = (c_index * snd_buf->channels) + channel; //Interleaved data
            
            if(index && (actual_index < snd_buf->samples))
                return snd_buf->data[actual_index];
        }
        
        return 0.f;
    }

    __attribute__((visibility("default"))) void jl_set_float_value_buf_SC(void* buf, float value, size_t index, size_t channel)
    {
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;

            size_t c_index = index - 1; //Julia counts from 1, that's why index - 1
            
            size_t actual_index = (c_index * snd_buf->channels) + channel; //Interleaved data
            
            if(index && (actual_index < snd_buf->samples))
            {
                snd_buf->data[actual_index] = value;
                return;
            }
        }
    }

    //Length of each channel
    __attribute__((visibility("default"))) int jl_get_frames_buf_SC(void* buf)
    {
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;
            return snd_buf->frames;
        }
            
        return 0;
    }

    //Total allocated length
    __attribute__((visibility("default"))) int jl_get_samples_buf_SC(void* buf)
    {
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;
            return snd_buf->samples;
        }

        return 0;
    }

    //Number of channels
    __attribute__((visibility("default"))) int jl_get_channels_buf_SC(void* buf)
    {
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;
            return snd_buf->channels;
        }
            
        return 0;
    }

    //Samplerate
    __attribute__((visibility("default"))) double jl_get_samplerate_buf_SC(void* buf)
    {
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;
            return snd_buf->samplerate;
        }
            
        return 0;
    }

    //Sampledur
    __attribute__((visibility("default"))) double jl_get_sampledur_buf_SC(void* buf)
    {
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;
            return snd_buf->sampledur;
        }
            
        return 0;
    }
}