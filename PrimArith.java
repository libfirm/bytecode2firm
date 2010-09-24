public class PrimArith {

	static void testSimple()
	{
		byte   b = Byte.MIN_VALUE;
		short  s = Short.MAX_VALUE;
		int    i = Integer.MIN_VALUE;
		long   l = Long.MAX_VALUE;
		float  f = Float.MIN_NORMAL;
		double d = Double.MIN_VALUE;
		
		long   res1 = b + s + i + l;
		double res2 = f +d;
		double res3 = res1 + res2;
		
		System.out.println("int arith: " + res1);
		System.out.println("float arith: " + res2);
		System.out.println("int/float comb.: " + res3);
	}
	
	static void testCompare()
	{
		int i1 = ((int) Math.random()) + 42;
		int i2 = ((int) Math.random()) + 42;
		boolean b1 = i1 == i2;
		System.out.println("int eq: " + b1);
		
		i2++;
		boolean b2 = i1 == i2;
		System.out.println("int neq: " + b2);
		
		
		long l1 = ((int) Math.random()) + 42L;
		long l2 = ((int) Math.random()) + 42L;
		b1 = l1 == l2;
		System.out.println("long eq: " + b1);
		
		l2++;
		b2 = l1 == l2;
		System.out.println("long neq: " + b2);
		
		boolean evil_eq = 0xFFFFFFFF00000000L == ((long)((int) Math.random()));
		System.out.println("long evil eq: " + evil_eq);
		
		float f1 = ((int) Math.random()) + 42.0f;
		float f2 = ((int) Math.random()) + 42.0f;
		b1 = f1 == f2;
		System.out.println("float eq: " + b1);
		
		f2 += 1;
		b2 = f1 == f2;
		System.out.println("float neq: " + b2);
		
		double d1 = ((int) Math.random()) + 42.0;
		double d2 = ((int) Math.random()) + 42.0;
		b1 = d1 == d2;
		System.out.println("double eq: " + b1);
		
		d2 += 1;
		b2 = d1 == d2;
		System.out.println("double neq: " + b2);
		
		boolean b3 = d1 < Double.NaN;
		System.out.println("double < NaN: " + b3);
		
		b3 = d2 > Double.NaN;
		System.out.println("double > NaN: " + b3);
	}
	
	static void testConversion()
	{
		int biggerThanByte = ((int) Math.random()) - 1234;
		int biggerThanShort = ((int) Math.random()) - 76789798;
		long biggerThanInt = ((int) Math.random()) + 0xdeadbeefbabeL;
		
		byte b = (byte) biggerThanByte;
		System.err.println("i2b: " + b);
		short s = (short) biggerThanShort;
		System.err.println("i2s: " + s);
		int i = biggerThanShort;
		long l = i;
		System.err.println("i2l: " + i);
		float f = i;
		System.err.println("i2f: " + f);
		double d = i;
		System.err.println("i2f: " + d);
		
		i = (int) l; 
		System.err.println("l2i: " + i);
		f = (float) l;
		System.err.println("l2f: " + f);
		d = (double) l;
		System.err.println("l2d: " + d);

		i = (int) f;
		System.err.println("f2i: " + i);
		l = (long) f;
		System.err.println("f2l: " + l);
		d = (double) f;
		System.err.println("f2d: " + d);
		
		i = (int) d;
		System.err.println("d2i: " + i);
		l = (long) d;
		System.err.println("d2l: " + l);
		f = (float) d;
		System.err.println("d2f: " + f);
	}
	
	public static void main(String args[])
	{
		testSimple();
		testCompare();
		testConversion();
	}
}
