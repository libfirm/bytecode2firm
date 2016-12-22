import java.util.Arrays;


class Data1 {
	boolean b;
	long l;
	
	Data1(int i) {l = i; b = i % 2 == 1;}
	public String toString() {
		return "D1: " + b + " " + l;
	}
}

class Data2 {
	String s;
	float f;
	
	Data2(int i) {s = "i is " + i; f = i;}
	public String toString() {
		return "D2: " + s + " " + f;
	}
}

public class InstanceVars
{
	Data1   d1  = new Data1(13);
	Data2[] d2s = new Data2[5];
	
	static Data1 d1a = new Data1(-1);  
	
	public static void main(String[] args)
	{
		InstanceVars iv1 = new InstanceVars();
		iv1.d2s[3] = new Data2(5);
		iv1.d2s[3].f = 3.14f;
		iv1.d2s[3].s = "Hello Data2";
		
		InstanceVars iv2 = iv1;
		System.out.println(iv2.d1);
		iv1.d1 = new Data1(10);
		System.out.println(iv2.d1);
		
		System.out.println(Arrays.toString(iv2.d2s));
		iv1.d2s = new Data2[] {new Data2(1), null, new Data2(2)};
		System.out.println(Arrays.toString(iv2.d2s));
		
		System.out.println(d1a);
	}
	
}