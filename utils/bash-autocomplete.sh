# Please add "source /path/to/bash-autocomplete.sh" to your .bashrc to use
# after you've added the git autocommplete bash script as well.

_git_apple_llvm_completion_filedir()
{
  # _filedir function provided by recent versions of bash-completion package is
  # better than "compgen -f" because the former honors spaces in pathnames while
  # the latter doesn't. So we use compgen only when _filedir is not provided.
  _filedir 2> /dev/null || COMPREPLY=( $( compgen -f ) )
}

# A sanity tester for completion support.
_git_apple_llvm___test_completion_sub() {
  local word="${COMP_WORDS[COMP_CWORD]}";
  COMPREPLY=( $( compgen -W "test_sub_a test_sub_b" -- "$word") )
}

_git_apple_llvm___test_completion() {
  local word="${COMP_WORDS[COMP_CWORD]}";
  COMPREPLY=( $( compgen -W "test_x test_y" -- "$word") )
}

# This function takes in a subcommand index, e.g. for git apple-llvm x it would
# be '2', and dispatches the completion over to _git_apple_llvm_x if such
# function exists.
#
# Returns 0 if the completion was dispatched, 1 otherwise.
_git_apple_llvm_completion_sub_command_dispatch()
{
  local max_words=$(expr $COMP_CWORD - 1)
  local command_index=$1
  if [[ ! $max_words < $command_index ]]; then
    local sub_command=""
    for i in `seq 2 $command_index`; do
      sub_command="${sub_command}_${COMP_WORDS[$i]}"
    done
    local completion_func="_git_apple_llvm${sub_command//-/_}"
    if declare -f $completion_func >/dev/null 2>/dev/null;
    then
      $completion_func
      return 0
    fi
  fi
  return 1
}

# Creates a completion reply that's based on the output of `--usage`.
_git_apple_llvm_completion_complete_based_on_usage()
{
  usage_flags=$(${COMP_WORDS[0]} apple-llvm $@ --usage | while read -r line
  do
    if [[ "$line" =~ "--" ]]; then
      local flag=$(echo "$line" | cut -f 1 -d " ");
      # FIXME: cut the '=', and support '=' completion better.
      printf "%s " $flag
    fi
  done)

  local word="${COMP_WORDS[COMP_CWORD]}";
  COMPREPLY=( $( compgen -W "$usage_flags" -- "$word") )
}

# Completion support for 'fwd' command.
_git_apple_llvm_fwd()
{
  _git_apple_llvm_completion_complete_based_on_usage fwd
}

# Generic completion dispatcher.
_git_apple_llvm()
{
  local word="${COMP_WORDS[COMP_CWORD]}";

  # Check if we can complete git apple-llvm x y with function _git_apple_llvm_x_y
  _git_apple_llvm_completion_sub_command_dispatch 3
  if [[ "$?" == 0 ]]; then
    return
  fi
  # Check if we can complete git apple-llvm x with function _git_apple_llvm_x
  _git_apple_llvm_completion_sub_command_dispatch 2
  if [[ "$?" == 0 ]]; then
    return
  fi

  # Otherwise: check if the '--complete' argument is supported.
  eval local path=${COMP_WORDS[0]}
  local invocation=""
  local max_words=$(expr $COMP_CWORD - 1)
  for i in `seq 1 $max_words`; do
    invocation="$invocation ${COMP_WORDS[$i]}"
  done

  $("$path" $invocation --complete 2>/dev/null 1>/dev/null)
  if [[ "$?" == 0 ]]; then
    local flags=$("$path" $invocation --complete)
    COMPREPLY=( $( compgen -W "$flags" -- "$word") )
    return
  fi

  # Fallback to file and directory completion.
  _git_apple_llvm_completion_filedir
}
