     interface I1                           {       void foo();  }
     interface I2 extends I1                {       void foo2(); }
     interface I3                           {       void foo2(); void foo3(); }
         class A implements I1              {public void foo() {System.out.println("A.foo()");}}
final    class B extends A                  {public void foo() {System.out.println("B.foo()");}}
abstract class C extends A implements I2,I3 {public void foo3() {System.out.println("C.foo3");}}
         class D extends C                  {public void foo2() {System.out.println("D.foo2()");}}


public class OOO
{
	static I1 s = new D();
	public static void main(String[] args)
	{
			I1 i1 = new B();
			i1.foo();
	
			I2 i2 = new D();
			i2.foo();
			i2.foo2();
			
			I3 i3 = (I3) i2;
			i3.foo3();
			i3.foo2();
			
			s.foo();
	}

}

