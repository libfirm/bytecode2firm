// There was a Bug in liboo that produced linker errors with this example.


interface Foobar {
	void foo();
}

class A
	implements Foobar
{
	public void foo() {}
}

class B extends A
	implements Foobar
{
	// inherit void foo()
}

class C extends B
	implements Foobar
{
	// inherit void foo()
	public void bar() {}
}

public class EntityCopies {
	public static void main(String[] args) {
		new C().foo();
	}
}
