public class SimpleArrayTest {
	public static void main(String[] args) {
		int[] arr = new int[10];
		System.out.println(arr.length);
		for (int i = 0; i < arr.length; ++i)
			arr[i] = 42;

		for (int i = 0; i < arr.length; ++i)
			System.out.println(arr[i]);
	}
}
