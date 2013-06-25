public class CreateObject {
	public int x;

	public String toString() {
		return "=" + x;
	}

	public static void main(String[] args) {
		CreateObject o = new CreateObject();
		o.x = 42;
		System.out.println(o.x);
		System.out.println(o);
	}
}
