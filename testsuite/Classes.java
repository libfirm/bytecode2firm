
public class Classes
{
	void foo() {}
	int i;
	
	static void testClassref()
	{
		Class<?> c1 = Classes.class;
		Classes c = new Classes();
		Class<?> c2 = c.getClass();
		
		System.out.println(c1);
		System.out.println(c2);
		System.out.println(c1 == c2);
	}
	
	public static void main(String[] args)
	{
		testClassref();
	}
}
