
class Element {
	byte unused;
	long l;
	
	public String toString() {
		return "I'm an Element, l=" + l;
	}
}

public class Arrays
{
	
	static void testArraysSimple()
	{
		int size = ((int)Math.random()) + 5;
		boolean[]	zs = new boolean[size++];
		byte[]		bs = new byte[size++];
		short[]		ss = new short[size++];
		int[]		is = new int[size++];
		long[]		ls = new long[size++];
		float[]		fs = new float[size++];
		double[]	ds = new double[size++];
		Object[]	os = new Object[size++];
		Element[]	es = new Element[size];
		
		int size_check = zs.length + bs.length + ss.length + is.length + ls.length + fs.length + ds.length + os.length + es.length;
		System.out.println("array_sizes = " + size_check);
		
		testBooleanArray(zs);
		testByteArray(bs);
		testShortArray(ss);
		testIntArray(is);
		testFloatArray(fs);
		testDoubleArray(ds);
		testObjectArray(os);
		testElementArray(es);
	}
	
	static void testBooleanArray(boolean[] a)
	{
		int m = a.length / 2;
		a[0] = true;
		a[m] = true;
		a[a.length-1] = true;
		System.out.println(java.util.Arrays.toString(a));
	}
	
	static void testByteArray(byte[] a)
	{
		int m = a.length / 2;
		a[0] = -13;
		a[m] = 117;
		a[a.length-1] = 23;
		System.out.println(java.util.Arrays.toString(a));
	}
	
	static void testShortArray(short[] a)
	{
		int m = a.length / 2;
		a[0] = -1013;
		a[m] = 10107;
		a[a.length-1] = 230;
		System.out.println(java.util.Arrays.toString(a));
	}
	
	static void testIntArray(int[] a)
	{
		int m = a.length / 2;
		a[0] = -130001;
		a[m] = Integer.MAX_VALUE;
		a[a.length-1] = 420000;
		System.out.println(java.util.Arrays.toString(a));
	}
	
	static void testLongArray(long[] a)
	{
		int m = a.length / 2;
		a[0] = -13*Integer.MIN_VALUE;
		a[m] = 1;
		a[a.length-1] = 77;
		System.out.println(java.util.Arrays.toString(a));
	}
	
	static void testFloatArray(float[] a)
	{
		int m = a.length / 2;
		a[0] = Float.NEGATIVE_INFINITY;
		a[m] = 37.2f;
		a[a.length-1] = 23.114e17f;
		System.out.println(java.util.Arrays.toString(a));
	}
	
	static void testDoubleArray(double[] a)
	{
		int m = a.length / 2;
		a[0] = Math.E;
		a[m] = Math.pow(Math.PI, 2) / 17.0;
		a[a.length-1] = Double.NaN;
		System.out.println(java.util.Arrays.toString(a));
	}
	
	static void testObjectArray(Object[] a)
	{
		int m = a.length / 2;
		a[0] = "Hallo Welt!";
		a[m] = 42;
		a[a.length-1] = new Arrays();
		System.out.println(java.util.Arrays.toString(a));
	}
	
	static void testElementArray(Element[] a)
	{
		int m = a.length / 2;
		a[0] = new Element();
		a[0].l = 0xdeadbeef;
		a[m] = new Element();
		a[m].l = a[0].l + 0xcafebabe;
		a[a.length-1] = new Element();
		System.out.println(java.util.Arrays.toString(a));
	}
	
	static void testMultiArray()
	{
		Object[][][] os = new Object[2][3][4];
		byte[][][]   bs = new byte[6][7][8];
		bs[1][2][3] = 5;
		System.out.println(bs[1][2][3]);
		System.out.println(bs[1][4][3]);
	}
	
	public static void main(String[] args)
	{
		testArraysSimple();
		testMultiArray();
	}
	
	public String toString()
	{
		return "I'm the Arrays testcase"; 
	}
}
