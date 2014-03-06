#include "types.h"

java_lang_String *_ZN4java4lang5Class7getNameEJPNS0_6StringEv(const java_lang_Class *this_)
{
	return _Z22_Jv_NewStringUtf8ConstP13_Jv_Utf8Const(this_->name);
}

jboolean _ZN4java4lang5Class11isPrimitiveEJbv(const java_lang_Class *this_)
{
	return (this_->vtable == (vtable_t*)-1)
	    && !(this_->accflags & ACCESS_FLAG_INTERFACE);
}

jboolean _ZN4java4lang5Class11isInterfaceEJbv(const java_lang_Class *this_)
{
	return (this_->accflags & ACCESS_FLAG_INTERFACE) != 0;
}

jboolean _ZN4java4lang5Class7isArrayEJbv(const java_lang_Class *this_)
{
	return this_->name->data[0] == '[';
}
