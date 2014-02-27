package java.io;

public class PrintStream {
	private static native void putbyte(byte b);

	public void println() {
		putbyte((byte)'\n');
	}

	public void println(String s) {
		for (int i = 0; i < s.length(); ++i) {
			putbyte((byte)s.charAt(i));
		}
		println();
	}
}
