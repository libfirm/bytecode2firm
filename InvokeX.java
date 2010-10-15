class Base
{
	       void  foo() { System.out.println("Base::foo()"); }
	static void sfoo() { System.out.println("Base::sfoo()"); }
	final  void ffoo() { System.out.println("Base::ffoo()"); }
}

class Sub extends Base
{
	       void  foo() { System.out.println("Sub::foo()"); }
	static void sfoo() { System.out.println("Sub::sfoo()"); }
}

abstract class ABase
{
	abstract void afoo();
}

class ASub extends ABase
{
	         void afoo() { System.out.println("ASub::afoo()"); }
}

class CBase
{
	long my_l;
	CBase(long l) { my_l = l; System.out.println("CBase::<init>(" + my_l + ")"); pfoo(); foo(); }
	
	        void  foo() { System.out.println("CBase::foo()"); }
	private void pfoo() { System.out.println("CBase::pfoo()"); }
}

class CSub extends CBase
{
	CSub(long l) { super(l); System.out.println("CSub::<init>(" + my_l + ")"); pfoo(); foo(); }
	
            void  foo() { System.out.println("CSub::foo()"); }
	private void pfoo() { System.out.println("CSub::pfoo()"); }
}

class Params
{
	static  void sfoo(int i, long l, float f, byte b, char c, boolean z, double d, Object o, short s)   { Params p = new Params(z, b, s, c, i, l, f, d, o); }
	             Params(boolean z, byte b, short s, char c, int i, long l, float f, double d, Object o) {                  pfoo(o, d, f, i, c, s, b, z, l); }
	private void pfoo(Object o, double d, float f, int i, char c, short s, byte b, boolean z, long l)   {                   foo(s, z, l, f, i, o, d, b, c); }
	        void  foo(short s, boolean z, long l, float f, int i, Object o, double d, byte b, char c)   { 
	        	System.out.println("boolean: " + z);
	        	System.out.println("byte: " + b);
	        	System.out.println("short: " + s);
	        	System.out.println("char: " + c);
	        	System.out.println("int: " + i);
	        	System.out.println("long: " + l);
	        	System.out.println("float: " + f);
	        	System.out.println("double: " + d);
	        	System.out.println("Object: " + o);
	        }
}


interface IFace
{
	int i = 0xdeadbeef;
	void ifoo();
}

interface IFace2
{
	void ifoo2();
}

class IFaceImpl1 implements IFace, IFace2
{
	public void ifoo2() { System.out.println("IFaceImp1::ifoo2(" + ")"); }
	public void ifoo()  { System.out.println("IFaceImp1::ifoo1(" + IFace.i + ")"); }
}

public class InvokeX
{
	
	static void testDynLink ()
	{
		Base base = new Base();
		base.foo();
		
		base      = new Sub();
		base.foo();
		base.ffoo();
		
		Base.sfoo();
		Sub.sfoo();
	}
	
	static void testAbstractClasses ()
	{
		ABase abase = new ASub();
		abase.afoo();
	}
	
	static void testConstr()
	{
		CBase cbase = new CBase(-1L);
		cbase = new CSub(42L);
	}
	
	static void testParams()
	{
		Params.sfoo(-42, ((long)Integer.MIN_VALUE) << 10, 3.14f, (byte)0xaf, 'm', false, Math.PI * Math.PI / 17, "Hello World", (short)1023);
	}
	
	static void testIFace()
	{
		IFaceImpl1 impl = new IFaceImpl1();
		IFace iface = impl;
		iface.ifoo();
		IFace2 iface2 = impl;
		iface2.ifoo2();
	}
	
	public static void main(String[] args)
	{
		testDynLink();
		testAbstractClasses();
		testConstr();
		testParams();
		testIFace(); 
	}
}
