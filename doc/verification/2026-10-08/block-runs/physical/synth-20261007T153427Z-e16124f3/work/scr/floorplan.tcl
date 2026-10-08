# floorplan.tcl — physical floorplan for the CGRA fabric.
#
# Sourced by build/timing/fmax when FLOORPLAN=1. Defines cgra_floorplan, which
# boxes the whole 4x4 PE array (u_array) into ONE clock region so the 16 DSP
# multipliers sit next to their own fabric logic and the nearest-neighbour mesh
# nets stay short. On a spread-out placement the datapath route delay dominates
# (~45% of the critical path); co-locating the array trades placement freedom for
# shorter, more predictable routes -- the term that caps Fmax here.
#
# The target region is chosen AUTOMATICALLY (the clock region with the most
# DSP48 sites), so this is part-agnostic: no hand-picked, easily-stale SLICE/DSP
# coordinates. A clock region on the xc7a100t/xc7a35t holds far more than the
# array needs (16 DSPs, ~5k LUTs), so containment is loose enough not to choke
# the router.
#
#   cgra_floorplan <board>
#
proc cgra_floorplan {board} {
    set cell [get_cells -quiet u_array]
    if { [llength $cell] == 0 } {
        puts "floorplan: cell 'u_array' not found -- skipping"
        return
    }

    # Pick the clock region richest in DSP48 sites (the array is DSP-bound).
    set best ""; set bestn 0
    foreach cr [get_clock_regions] {
        set n [llength [get_sites -quiet -of_objects $cr -filter {SITE_TYPE =~ DSP48*}]]
        if { $n > $bestn } { set bestn $n; set best $cr }
    }
    if { $best eq "" || $bestn < 16 } {
        puts "floorplan: no clock region with >=16 DSP48 sites -- skipping"
        return
    }

    catch { delete_pblocks -quiet pb_array }
    create_pblock pb_array
    add_cells_to_pblock pb_array $cell -clear_locs
    resize_pblock pb_array -add "CLOCKREGION_[get_property NAME $best]"
    # Keep cells inside the region but let the router use tracks outside it: the
    # win is short logic placement, not a routing fence.
    set_property CONTAIN_ROUTING 0 [get_pblocks pb_array]
    puts "floorplan: pb_array -> clock region $best ($bestn DSP48 sites)"
}
