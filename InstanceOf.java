class X { }
class Y extends X { }
class Z extends Y { }

public class InstanceOf
{
	static void testInstanceOf()
	{
		X x = new X();
		Y y = new Y();
		Z z = new Z();
		System.out.println(x instanceof X);
		System.out.println(y instanceof X);
		System.out.println(z instanceof X);
		
		System.out.println(x instanceof Y);
		System.out.println(y instanceof Y);
		System.out.println(z instanceof Y);
		
		System.out.println(x instanceof Z);
		System.out.println(y instanceof Z);
		System.out.println(z instanceof Z);
	}
	
	static void testCheckCast()
	{
		java.util.List strings = new java.util.ArrayList();
		strings.add("hallo");
		
		StringBuffer sb = new StringBuffer();
		sb.append(strings.get(0)).append(" welt");
		
		strings.add(sb.toString());
		strings.add(new Object());
		String s = (String)strings.get(1);
		
		//String s2 = (String)strings.get(2); // uncomment this to trigger a failing checkcast.
		
		System.out.println(s);
	}

	public static void main(String[] args) {
		testInstanceOf();
		testCheckCast();
	}
}
