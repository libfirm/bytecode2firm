public class Strings 
{
	static void testStringBuilder()
	{
		StringBuilder sb = new StringBuilder();
		sb.append("Hallo");
		System.out.println(sb.toString());
		sb.append(1);
		System.out.println(sb.toString());
		sb.append("Welt");
		System.out.println(sb.toString());
		sb.insert(3, "XXX");
		System.out.println(sb.toString());
		sb.reverse();
		System.out.println(sb.toString());
		sb.replace(1, 4, "YY");
		System.out.println(sb.toString());
	}
	
	static void testString()
	{
		String orig_s = "Hallo Welt";
		String s = orig_s;
		System.out.println(s);
		s = s.substring(6);
		System.out.println(s);
		s = s.concat(" ").concat(orig_s.substring(0,5));
		System.out.println(s);
		s = s.toUpperCase();
		System.out.println(s);
		s = s.toLowerCase();
		System.out.println(s);
		char[] cs = s.toCharArray();
		System.out.println(cs[3]);
		cs[4] = 'X';
		s = new String(cs);
		System.out.println(s);
		String[] ss = s.split("X");
		s = String.format("%d %s %s", ss.length, ss[0], ss[1]);
		System.out.println(s);
	}
	
	public static void main(String[] args)
	{
		testString();
		testStringBuilder();
	}
	
}

