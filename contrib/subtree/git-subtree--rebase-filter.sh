#!/bin/sh

set -e

RECIPE=$1

while read cmd args
do
	case "$cmd" in
	(merge)
		set -- ${args%% # *}
		shift # -C
		if [ $# -eq 2 ]
		then
			COMMIT=$1

			LEFT_TREE=$(git rev-parse $COMMIT^1^{tree})
			# that sould be $2
			RIGHT_PARENT=$(git rev-parse $COMMIT^2)
			RIGHT_TREE=$(git rev-parse $RIGHT_PARENT^{tree})

			PREFIX="$(git ls-tree -t -r $COMMIT | sed -n -e "s/^040000 tree $RIGHT_TREE.// p")"

			if [ -z "$PREFIX" ]
			then
				echo "$cmd $args"
			fi

			if [ -z "$(git ls-tree $LEFT_TREE -- "$PREFIX")" ]
			then
				echo "exec git subtree --force add -P '$PREFIX' $RIGHT_PARENT"
			else
				echo "exec git merge -Xsubtree='$PREFIX' $RIGHT_PARENT"
			fi

		else
			echo "$cmd $args"
		fi
	;;
	(*)
		echo "$cmd $args"
	;;
	esac
done <"$RECIPE" >"$RECIPE".new
mv -f "$RECIPE".new "$RECIPE"

eval exec "$SUBTREE_REBASE_ORIG_EDITOR \"\$RECIPE\""
