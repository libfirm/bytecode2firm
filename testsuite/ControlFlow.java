
public class ControlFlow
{
	static void ok(String s) {System.out.println(s + " ok");}
	static void fail(String s) {System.out.println(s + " fail");}
	
	static void testIf()
	{
		boolean mytrue = Math.random() >= 0;
		boolean myfalse = Math.random() < 0;
		
		if (mytrue) {
			ok("if_1");
		} else {
			fail("if_1");
		}
		ok("if_1a");
		
		if (myfalse) {
			fail("if_2");
		} else {
			ok("if_2");
		}
		ok("if_2a");
		
		if (mytrue || myfalse) {
			ok("if_3");
		} else {
			fail("if_3");
		}
		ok("if_3a");
		
		if (myfalse && mytrue) {
			fail("if_4");
		} else {
			ok("if_4");
		}
		ok("if_4a");
		
		if (mytrue) {
			if (myfalse) {
				fail("if_51");
			} else {
				if (myfalse || mytrue) {
					ok("if_51x");
				} else {
					fail("if_51y");
				}
			}
			ok("if_51a");
		} else {
			if (mytrue) {
				fail("if_52x");
			} else {
				fail("if_52y");
			}
		}
		ok("if_5a");
	}
	
	static void testFor()
	{
		for (int i=0; i<10; i++) {
			ok("for_1 " + i);
		}
		ok("for_1 end");
		
		for (int i=11; i<10; i++) {
			fail("for_2" + i);
		}
		ok("for_2 end");
	}
	
	static void testWhile()
	{
		int i = 0;
		while (i<10) {
			ok("while_1 " + i);
			i++;
		}
		ok("while_1 end=" + i);
		
		while (i<10) {
			fail("while_2" + i);
		}
		ok("while_2 end=" + i);
	}
	
	static void testDoWhile()
	{
		int i = 0;
		
		do {
			ok("dowhile_1 " + i);
			i++;
		} while (i<10);
		ok("dowhile_1 end=" + i);
		
		do {
			ok("dowhile_2 " + i);
			i++;
		} while (i<10);
		ok("dowhile_2 end=" + i);
	}
	
	static void testNestedLoops()
	{
		int i = 0;
		int j = 10;
		int k = -10;
		
		for (i=0; i<10; i++) {
			do {
				while (k < 0) {
					ok ("nested " + i + "," + j + "," + k);
					k++;
				}
				k = -10;
				j--;
			} while (j >= 0);
		}
	}
	
	static void testSwitch()
	{
		int i = ((int)Math.random()) + 3; // = 3
		
		switch (i) {
		case 0: fail("switch_1 0"); break;
		case 1: fail("switch_1 1"); break;
		case 2: fail("switch_1 2"); break;
		case 3: ok("switch_1 3"); break;
		case 4: fail("switch_1 4"); break;
		default: fail("switch_1 def");
		}
		ok("switch_1a");
		
		switch (i) {
		case 2: fail("switch_2 2");
		case 3: ok("switch_2 3");
		case 4: ok("switch_2 4");
		default: ok("switch_2 def");
		}
		ok("switch_2a");
		
		i = i+4; // = 7
		switch (i) {
		case -5: fail("switch_3 -5"); break;
		case 10: fail("switch_3 10"); break;
		case 1024: fail("switch_3 1024"); break;
		case Integer.MAX_VALUE: fail("switch_3 Integer.MAX_VALUE"); break;
		//case Integer.MIN_VALUE: fail("switch_3 Integer.MIN_VALUE"); break;
		case 7: ok("switch_3 4"); break;
		default: ok("switch_3 def");
		}
		ok("switch_3a");
		
		i = i+3; // = 10
		switch (i) {
		case -5: fail("switch_4 -5"); break;
		case 10: ok("switch_4 10");
		case 1024: ok("switch_4 1024");
		case Integer.MAX_VALUE: ok("switch_4 Integer.MAX_VALUE");
		case 7: ok("switch_4 4");
		default: ok("switch_4 def");
		}
		ok("switch_4a");
		
		switch (i) {
		case -5: fail("switch_5 -5"); break;
		case 10: ok("switch_5 10");
		case 1024:
			ok("switch_5 1024");
			int j = i + 15; // = 25
			switch (i) {
			case 14: fail("switch_51 14"); break;
			case 15: fail("switch_51 15"); break;
			}
			ok("switch_51a");
		case Integer.MAX_VALUE: ok("switch_5 Integer.MAX_VALUE"); break;
		case 7: fail("switch_5 " + 4);
		default: fail("switch_5 def");
		}
		ok("switch_5a");
	}
	
	public static void main(String args[])
	{
		testIf();
		testFor();
		testWhile();
		testDoWhile();
		testNestedLoops();
		testSwitch();
	}
}
