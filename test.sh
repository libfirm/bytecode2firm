# Compile, run and check outputs of a single test
#
# Expects the following environment variables to be set:
#   file: input sourcefile
#   logfile
#   TESTIO
#   TEST_COMPILER
#   TEST_CFLAGS
#   CFLAGS
#   FILE_FLAGS
#   LINKFLAGS
#   OUTPUTDIR
#   BUILDDIR
#   RUNEXE

. ./helpers.sh

do_test() {
	BASEDIR="`dirname ${file}`"
	name="`basename $file .java`"
	if [ -e "$BASEDIR/config" ]; then
		. "$BASEDIR/config"
	fi
	dirprefix=`echo "${BASEDIR}" | sed -e "s/\\//_/"`

	echo "Results for \"$name\"" > "$logfile"
	echo >> "$logfile"
	
	# build+execute test executable
	echo "*** bc2firm Compile" >> "$logfile"
	cmd="javac ${file}"
	if ! execute_cmd "$cmd" ""; then
		return 1
	fi

	classname=`echo $BASEDIR/$name | sed "s|^$dirprefix/||"`
	classpath="$dirprefix"
	bootclasspath="$HOME/Src/gcj-classes"
	output_file="$BASEDIR/$name"
	cmd="bytecode2firm -cp $classpath -bootclasspath $bootclasspath -o $output_file $classname"
	if ! execute_cmd "$cmd" ""; then
		return 1
	fi

	echo "*** bc2firm Run" >> "$logfile"
	res_test="$OUTPUTDIR/${dirprefix}_${name}_result_test.txt"
	cmd="${RUNEXE} ./${output_file} > $res_test"
	if ! execute_cmd "$cmd" ""; then
		return 1
	fi

	# compare results
	echo "*** Compare Results" >> "$logfile"
	res_ref="${file}.ref"
	cmd="diff -U100000 $res_test $res_ref > $OUTPUTDIR/${dirprefix}_${name}_result_diff.txt"
	execute_cmd "$cmd" "wrong results" || return 1
	
	# results are the same, so remove unnecessary files
	rm "$OUTPUTDIR/${dirprefix}_${name}_result_diff.txt"
	mv -f "$OUTPUTDIR/${dirprefix}_${name}_result_test.txt" "$OUTPUTDIR/${dirprefix}_${name}_result.txt"

	return 0
}
