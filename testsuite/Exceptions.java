class X extends Exception { }
class Y extends X { }
class Z extends Y{}

public class Exceptions 
{
	static void testException()
	{
		Exception e = new Exception();
		e.printStackTrace(System.out);
	}
	
	static void testThrow()
	{
		try
		{
			throw new Y();
		} catch(X x) {
			System.out.println("caught X");
		} finally {
			System.out.println("finally");
		}
	}
	
	public static void main(String[] args) {
		testException();
		testThrow();
	}
}
