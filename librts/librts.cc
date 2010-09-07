/*
 * Runtime library for bytecode2firm
 */

// Dummy functions.

#include <stdio.h>
#include <stdlib.h>

extern "C" {
	void __abstract_method(void)
	{
		fprintf(stderr, "Something's wrong, cannot call an abstract method");
		exit(-1);
	}

	void __invokeinterface_nyi(void)
	{
		fprintf(stderr, "INVOKEINTERFACE not yet implemented");
		exit(-1);
	}
}

// Minimal initialization required to use classes from libgcj.

extern "C" {
	void __gcj_init(void);
}

namespace gcj {

	extern void* void_signature;
	extern void* clinit_name;
	extern void* init_name;
	extern void* finit_name;
}

namespace java {
	namespace lang {
		namespace System {
			extern void* class$;
		}
		namespace String {
			extern void* class$;
		}
		class Class;
	}
}

typedef java::lang::Class jclass;

extern "C" {
	extern void  _Jv_InitClass(void*);
	extern int   _Jv_CreateJavaVM(void*);
	extern jclass _Jv_byteClass;
	extern jclass _Jv_shortClass;
	extern jclass _Jv_intClass;
	extern jclass _Jv_longClass;
	extern jclass _Jv_booleanClass;
	extern jclass _Jv_charClass;
	extern jclass _Jv_floatClass;
	extern jclass _Jv_floatClass;
	extern jclass _Jv_doubleClass;
	extern jclass _Jv_voidClass;
}

extern void* _Jv_makeUtf8Const(const char*, int);
extern void  _Jv_InitGC(void);
extern void  _Jv_InitPrimClass(java::lang::Class*, const char*, char, int);

void __gcj_init(void) {

  _Jv_InitGC();

  using namespace gcj;
  void_signature = _Jv_makeUtf8Const ("()V", 3);
  clinit_name = _Jv_makeUtf8Const ("<clinit>", 8);
  init_name = _Jv_makeUtf8Const ("<init>", 6);
  finit_name = _Jv_makeUtf8Const ("finit$", 6);

  _Jv_InitPrimClass (&_Jv_byteClass,    "byte",    'B', 1);
  _Jv_InitPrimClass (&_Jv_shortClass,   "short",   'S', 2);
  _Jv_InitPrimClass (&_Jv_intClass,     "int",     'I', 4);
  _Jv_InitPrimClass (&_Jv_longClass,    "long",    'J', 8);
  _Jv_InitPrimClass (&_Jv_booleanClass, "boolean", 'Z', 1);
  _Jv_InitPrimClass (&_Jv_charClass,    "char",    'C', 2);
  _Jv_InitPrimClass (&_Jv_floatClass,   "float",   'F', 4);
  _Jv_InitPrimClass (&_Jv_doubleClass,  "double",  'D', 8);
  _Jv_InitPrimClass (&_Jv_voidClass,    "void",    'V', 0);
  
  _Jv_InitClass(&java::lang::String::class$);
  _Jv_InitClass(&java::lang::System::class$);
}

// Startup code
extern "C" {
	
	extern void __bc2firm_main(void*);
	
	int main(int argc, char **argv)
	{
		(void) argc;
		(void) argv;

		__gcj_init();
		__bc2firm_main(NULL);
	}

}
