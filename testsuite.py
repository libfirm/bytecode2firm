from sisyphus import test
import os
from glob import glob
from sisyphus.test.test   import Test, Environment
from sisyphus.test.steps  import execute, step_execute
from sisyphus.test.checks import check_retcode_zero, create_check_reference_output

def step_compile_java(environment):
	"""Compile java file with java compiler"""
	cmd = "%(javac)s %(testname)s" % environment
	return execute(environment, cmd, timeout=240)

def step_compile_class(environment):
	"""Compile class file with bytecode2firm"""
	testname = environment.testname
	assert testname.endswith(".java")
	environment.classname = testname[:-5]
	environment.executable = "%(builddir)s/%(classname)s.exe" % environment
	cmd = "%(bc2firm)s %(classname)s %(bc2firmflags)s -o %(executable)s" % environment
	return execute(environment, cmd, timeout=240)

def make_bc2firm_test(filename):
	test = Test(filename)
	test.add_step("compile_java", step_compile_java, checks=[
		check_retcode_zero,
	])
	test.add_step("compile_class", step_compile_class, checks=[
		check_retcode_zero,
	])
	test.add_step("execute", step_execute, checks=[
		check_retcode_zero,
		create_check_reference_output(filename + ".ref"),
	])
	return test

def setup_argparser(argparser, default_env):
	group = argparser.add_argument_group("bytecode2firm")
	group.add_argument("--javac", dest="javac",
	                   help="Use JAVAC to compile Java programs", metavar="JAVAC")
	group.add_argument("--bc2firm", dest="bc2firm",
	                   help="Use BC2FIRM to compile Java class files", metavar="BC2FIRM")
	group.add_argument("--bc2firmflags", dest="bc2firmflags",
	                   help="Use flags to compile Java class files")
	default_env.set(
		javac="javac",
		bc2firm="bytecode2firm",
		bc2firmflags="",
	)

test.suite.add_argparser_setup(setup_argparser)

tests = []
for filename in glob("*.java"):
	tests.append(make_bc2firm_test(filename))

env = Environment(
	expect_url="fail_expectations.log",
)
test.suite.make("bc2firm-tests", tests=tests, environment=env, default=True)
