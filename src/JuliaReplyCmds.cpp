#include "JuliaReplyCmds.h"

/* Reply functions to sclang when a "/done" command is sent from "/julia_load", "/julia_free", etc..
This is where all the inputs/outputs/name informations are sent back to sclang from scsynth */

/**************/
/* JuliaReply */
/**************/

JuliaReply::JuliaReply(int OSC_unique_id_)
{
    count_char = 0;
    OSC_unique_id = OSC_unique_id_;
}

/* Using pointers to the buffer, shifted by count_char. */
int JuliaReply::append_string(char* buffer_, size_t size, const char* string)
{
    return snprintf(buffer_, size, "%s\n", string);
}

//for id
int JuliaReply::append_string(char* buffer_, size_t size, int value)
{
    return snprintf(buffer_, size, "%i\n", value);
}

//Exit condition. No more VarArgs to consume
void JuliaReply::create_done_command() 
{
    return;
}

int JuliaReply::get_OSC_unique_id()
{
    return OSC_unique_id;
}

char* JuliaReply::get_buffer()
{
    return buffer;
}

/**************************/
/* JuliaReplyWithLoadPath */
/**************************/

JuliaReplyWithLoadPath::JuliaReplyWithLoadPath(int OSC_unique_id_, const char* julia_load_path_) : JuliaReply(OSC_unique_id_)
{
    //std::string performs deep copy on char*
    julia_load_path = julia_load_path_;
}

const char* JuliaReplyWithLoadPath::get_julia_load_path()
{
    return julia_load_path.c_str();
}

/************************/
/* JuliaReceiveObjectId */
/************************/

JuliaReceiveObjectId::JuliaReceiveObjectId(int julia_object_id_)
{
    julia_object_id = julia_object_id_;
}

int JuliaReceiveObjectId::get_julia_object_id()
{
    return julia_object_id;
}