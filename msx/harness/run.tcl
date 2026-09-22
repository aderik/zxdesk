# ZX Desk for MSX: openMSX driver script.
#
# Runs with the throttle off, executes a step list written by
# msxtest.py, dumps VRAM and work RAM to files and takes a screenshot.
# Everything the harness asserts on comes out of the dumps; the
# screenshot is for people.
#
# Environment:
#   MSXTEST_OUT    directory for vram.bin, ram.bin, shot.png, log.txt
#   MSXTEST_STEPS  Tcl file with a `steps` list of {frames command}
#
# openMSX swallows Tcl errors raised inside `after` callbacks and then
# runs forever, so every callback is wrapped and the realtime guard
# below is the last line of defence.

set out $::env(MSXTEST_OUT)
set log [open $out/log.txt w]
proc note {msg} { puts $::log $msg; flush $::log }

set throttle off
after realtime 120 { note "TIMEOUT"; exit 3 }

proc key_down {row mask} { keymatrixdown $row $mask }
proc key_up {row mask} { keymatrixup $row $mask }

# Moves the Xvfb pointer, which openMSX's plugged mouse turns into
# joystick-port nibbles. The pointer has to be inside the window first.
proc mouse_move {dx dy} { exec xdotool mousemove_relative -- $dx $dy }

proc dump {} {
    set f [open $::out/vram.bin wb]
    puts -nonewline $f [debug read_block VRAM 0 16384]
    close $f
    set f [open $::out/ram.bin wb]
    puts -nonewline $f [debug read_block memory 0xC000 0x4000]
    close $f
    binary scan [debug read_block memory 0x4000 8] H* at4000
    note "dumped at [machine_info time], pc [reg PC], 4000: $at4000, slots [debug read_block {slotted memory} 0 0]"
}

proc finish {} {
    catch {dump} err
    catch {screenshot $::out/shot.png} err
    note "done"
    close $::log
    exit 0
}

proc run_steps {steps} {
    if {[llength $steps] == 0} { finish; return }
    lassign [lindex $steps 0] frames cmd
    set rest [lrange $steps 1 end]
    after time [expr {$frames / 50.0}] [list step_cb $cmd $rest]
}

proc step_cb {cmd rest} {
    if {[catch {uplevel #0 $cmd} err]} { note "STEP ERROR in {$cmd}: $err"; exit 4 }
    note "step: $cmd"
    run_steps $rest
}

# `after boot` fires at power-on, not when the BIOS is done: C-BIOS shows
# its logo for seconds first. So the steps start when the ROM's marker
# appears in work RAM, and the step clock counts from there.
proc wait_ready {} {
    if {[debug read_block memory 0xC000 5] eq "ZXMSX"} {
        note "ready at [machine_info time]"
        run_steps $::steps
    } else {
        after frame wait_ready
    }
}

after boot {
    if {[catch {source $::env(MSXTEST_STEPS)} err]} { note "STEPS ERROR: $err"; exit 4 }
    wait_ready
}
