package java.io;

public class PrintStream {
	private static native void putchar(char c);

	public void println() {
		putchar('\n');
	}

	public native void print(int i);

	public void print(String s) {
		for (int i = 0; i < s.length(); ++i) {
			putchar(s.charAt(i));
		}
	}

	public void print(Object o) {
		print(o.toString());
	}

	public void print(boolean b) {
		print(b ? "true" : "false");
	}

	public void print(char c) {
		putchar(c);
	}

	public void print(long l) {
		print(Long.toString(l));
	}

	public final void print(float f) {
		print(Float.toString(f));
	}

	public void print(double d) {
		print(Double.toString(d));
	}

	public void println(String s) {
		print(s);
		println();
	}

	public void println(int i) {
		print(i);
		println();
	}

	public void println(Object o) {
		print(o);
		println();
	}

	public void println(boolean b) {
		print(b);
		println();
	}

	public void println(char c) {
		print(c);
		println();
	}

	public void println(long l) {
		print(l);
		println();
	}

	public final void println(float f) {
		print(f);
		println();
	}

	public void println(double d) {
		print(d);
		println();
	}
}
