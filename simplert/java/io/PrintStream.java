package java.io;

public class PrintStream {
	private static native void putbyte(byte b);

	public void println() {
		putbyte((byte)'\n');
	}

	public native void print(int i);

	public void print(String s) {
		for (int i = 0; i < s.length(); ++i) {
			putbyte((byte)s.charAt(i));
		}
	}

//	public void print(Object o) {
//		print(o.toString());
//	}

	public void println(String s) {
		print(s);
		println();
	}

	public void println(int i) {
		print(i);
		println();
	}
}
