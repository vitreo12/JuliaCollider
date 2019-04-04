#pragma once

#include "RTClassAlloc.hpp"
#include <string>

/* Reply functions to sclang when a "/done" command is sent from "/julia_load", "/julia_free", etc..
This is where all the inputs/outputs/name informations are sent back to sclang from scsynth */

//Not the safest way. GetJuliaDefs could simply run out of the characters
#define JULIA_CHAR_BUFFER_SIZE 1000

/**************/
/* JuliaReply */
/**************/

class JuliaReply : public RTClassAlloc
{
    public:
        JuliaReply(int OSC_unique_id_);

        ~JuliaReply(){}

        /* Using pointers to the buffer, shifted by count_char. */
        int append_string(char* buffer_, size_t size, const char* string);
        
        //for id
        int append_string(char* buffer_, size_t size, int value);

        /* //for jl_unbox_int64
        int append_string(char* buffer_, size_t size, long value)
        {
            return snprintf(buffer_, size, "%ld\n", value);
        }

        //for id
        int append_string(char* buffer_, size_t size, int value)
        {
            return snprintf(buffer_, size, "%lu\n", value);
        } */

        //Exit condition. No more VarArgs to consume
        void create_done_command();

        template<typename T, typename... VarArgs>
        void create_done_command(T&& arg, VarArgs&&... args);

        int get_OSC_unique_id();

        char* get_buffer();

    private:
        char buffer[JULIA_CHAR_BUFFER_SIZE];
        int count_char;
        int OSC_unique_id; //sent from SC. used for OSC parsing
};

//Needs to be in same file of declaration...
template<typename T, typename... VarArgs>
void JuliaReply::create_done_command(T&& arg, VarArgs&&... args)
{    
    //Append string to the end of the previous one. Keep count of the position with "count_char"
    count_char += append_string(buffer + count_char, JULIA_CHAR_BUFFER_SIZE - count_char, arg); //std::forward<T>(arg...) ?

    //Call function recursively
    if(count_char && count_char < JULIA_CHAR_BUFFER_SIZE)
        create_done_command(args...); //std::forward<VarArgs>(args...) ?
}

/**************************/
/* JuliaReplyWithLoadPath */
/**************************/

class JuliaReplyWithLoadPath : public JuliaReply
{
    public:
        JuliaReplyWithLoadPath(int OSC_unique_id_, const char* julia_load_path_);

        const char* get_julia_load_path();

    private:
        std::string julia_load_path;
};

/************************/
/* JuliaReceiveObjectId */
/************************/

class JuliaReceiveObjectId : public RTClassAlloc
{
    public:
        JuliaReceiveObjectId(int julia_object_id_);

        int get_julia_object_id();

    private:
        int julia_object_id;
};
