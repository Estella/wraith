; $Id$
ON *:LOAD:{ 
  echo -a Wraith authorization script, for mIRC 5.8 and up, by lordares loaded (MSG/DCC/Psybnc support).
  helpauth 
}

alias helpauth {
  echo -a Type /setauth
  echo -a /auth -Auth string <botnick> <- That will return the md5 hash. (use for telnet possibly)
  echo -a /showauth [botnick] will show what settings will be used.
}

alias setauth {
  if (!$3) {
    echo -a usage: /setauth password secpass authkey <botnick>
    echo -a <botnick> is optional if the authkey specified is for that bot only...
    echo -a Use a . in place of a param if you do not have one set yet. (pass excluded)
  }
  else {
    if ($4) {
      set %w.pass. $+ $4 $1
      if ($2 != .) {
        set %w.secpass. $+ $4 $2
      } 
      else {
        set %w.secpass. $+ $4
      }
      if ($3 != .) {
        set %w.authkey. $+ $4 $3
      } 
      else {
        set %w.authkey. $+ $4
      }
    }
    else {
      set %w.pass $1
      if ($2 != .) {
        set %w.secpass $2
      } 
      else {
        set %w.secpass
      }
      if ($3 != .) {
        set %w.authkey $3
      } 
      else {
        set %w.authkey
      }
    }
    echo -a set!
  }
}

ALIAS showauth {
  if ($1) {
    var %s ( $+ $1 $+ )
  }
  echo -a %s PASS: $wpass($1) :: SECPASS: $wsecpass($1) :: AUTHKEY: $wauthkey($1)
}

ALIAS -l psy {
  if ($1 == $chr(40) || $1 == $chr(41)) {
    return 1
  }
}

ON *:CHAT:*:{
  var %c = %auth. [ $+ [ $nick ] ]
  if (($1 === -Auth || $1 === ��-Auth) && $len($2) == 50) {
    msg =$nick +Auth $wmd5($2 $+ $wsecpass($3) $+ $wauthkey($3))
  }
}

ON *:TEXT:auth*:?:{
  var %c = %auth. [ $+ [ $nick ] ]
  if (!$3) {
    ;if this is a MSG not psybnc DCC, and we arent cleared to auth with them, IGNORE.
    if (!$psy($left($nick, 1)) && !%c) {
      return
    }
    if ($right($1,1) == . && %c) {
      msg $nick auth $wpass($2)
    } 
    elseif ($right($1,1) == ! && %c) {
      msg $nick auth $wpass($2) %myuser
    }
  }
}

ON *:TEXT:*:?:{ 
  var %c = %auth. [ $+ [ $nick ] ]
  if (!$psy($left($nick, 1)) && !%c) { return }
  if (($1 === -Auth || $1 === ��-Auth) && $len($2) == 50) {
    msg $nick +Auth $wmd5($2 $+ $wsecpass($3) $+ $wauthkey($3))
  }
}

ALIAS -l wraith {
  if ($eval(% $+ $1 $+ . $+ $2,2) || $var($eval(% $+ $1 $+ . $+ $2,1))) {
    return $eval(% $+ $1 $+ . $+ $2,2)
  }
  else {
    return $eval(% $+ $1,2)
  }
}

alias wauthkey { return $wraith(w.authkey,$1) }
alias wpass { return $wraith(w.pass,$1) }
alias wsecpass { return $wraith(w.secpass,$1) }

alias wmd5 {
  if ($version < 5.8) {
    echo 8 -a This script will only work for mIRC 5.8 and up.
  }
  if ($version >= 6.03) {
    return $md5($1)
  } 
  else {
    if (!$exists($nofile($script) $+ /md5.dll)) { 
      echo 4 -a You need to place md5.dll in $nofile($script) for this to work.
      halt
    }
    return $dll($nofile($script) $+ /md5.dll,md5,$1)
  }
}

ALIAS auth { 
  ; stupid ircn.
  if ($gettok($1-, 0, 160) > $gettok($1-, 0, 32)) {
    tokenize 160 $1-
  }
  if ($1 != -Auth || !$2) { 
    echo 8 -a Usage: /auth -Auth string botname 
    echo 8 -a botname is optional. 
  }
  else {
    echo +Auth $wmd5($2 $+ $wsecpass($3) $+ $wauthkey($3))
  }
}
ALIAS msg { 
  if ($1 !ischan && $2 === auth? && !$psy($left($1,1))) { set -u30 %auth. $+ $1 1 }
  msg $1-
}
ON *:INPUT:?:{ if ($1 === auth? && !$psy($left($target,1))) { set -u30 %auth. $+ $target 1 } }
