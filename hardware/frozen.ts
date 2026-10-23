// Placement freeze (2026-10-15): the reviewed seed-19 poses of every constraint-placed part outside the MCU nest and PIXEL block (D_C1 too, since 2026-10-20).
// Moving the nest re-solves the whole board (85 of 152 parts moved in a --no-route trial), which breaks the locked buck, trunk and channel copper
// and the reviewed layout. Generated from the reviewed build (kicad/proptroller.ir.json at the time); each part keeps its constraints, which the
// oracle still checks. Regenerate (or delete an entry to free a part) only together with a re-review.
// Rev B (SAF-1, 2026-10-20): the relay row, the buck output caps and TP2 / TP5 moved by hand for the 3.0 mm contact-island margin
// (marked below); D_FB1 / D_FB2 and the DNP snubbers R_SN / C_SN were removed.
export const FROZEN: Record<string, readonly [number, number, number]> = {
  J_PWR: [107.600, 14.045, 270],
  J_IN: [97.500, 5.895, 180],
  D_TVS: [90.050, 24.300, 0],
  C_BK1: [91.700, 30.800, 180],
  C_BK2: [82.000, 33.650, 90],
  U_BK: [29.500, 58.500, 180],
  C_BI1: [35.000, 59.600, 270],   // 2026-10-21 (buck ground): upright beside pin 2, VIN north, GND south (was 37.7, 58.1, 0)
  C_BI2: [37.350, 59.600, 270],   // 2026-10-21 (buck ground): upright beside C_BI1 (was 37.5, 54.85, 0)
  C_BST: [31.750, 62.200, 180],
  D_CT1: [20.550, 62.600, 180],
  D_CT2: [20.850, 57.150, 90],
  L_BK: [30.000, 70.250, 270],
  C_BO1: [22.300, 73.600, 180],   // 2026-10-21 (buck ground): west of L_BK pad 2, GND west (was 36.55, 66.4, 270)
  C_BO2: [22.300, 76.400, 180],   // 2026-10-21 (buck ground): west of L_BK pad 2, GND west (was 36.55, 61.65, 270)
  R_RT: [32.350, 54.050, 90],
  R_FB1: [24.700, 54.800, 0],
  R_FB2: [23.850, 58.400, 270],
  R_CP: [24.400, 52.150, 90],
  C_CP: [27.100, 51.250, 0],
  C_CF: [28.650, 54.150, 0],
  FB_BK: [91.650, 41.950, 180],
  J_O1: [79.900, 74.105, 0],
  J_O2: [92.250, 74.105, 0],
  J_O3: [104.600, 74.105, 0],
  J_O4: [116.950, 74.105, 0],
  J_R1: [43.020, 74.105, 0],   // rev B (SAF-1): relay pitch 17.0 -> 18.1 mm (was 43.92)
  Q_R1: [58.750, 42.050, 180],
  R_RB1: [56.050, 44.800, 0],
  R_RP1: [60.550, 44.800, 0],
  R_RL1: [65.000, 43.300, 0],
  D_RL1: [62.350, 41.550, 270],
  J_R2: [61.120, 74.105, 0],   // rev B (SAF-1): was 60.92
  D_C2: [70.300, 45.000, 0],
  Q_R2: [75.500, 43.250, 0],
  R_RB2: [70.500, 42.300, 0],
  R_RP2: [75.850, 40.550, 0],
  R_RL2: [83.500, 43.250, 180],
  D_RL2: [80.100, 43.250, 0],
  J_T1: [12.550, 71.225, 90],
  J_T2: [31.000, 8.775, 270],
  J_C1: [42.600, 8.775, 270],
  J_C2: [54.200, 8.775, 270],
  SW_U1: [127.350, 53.350, 90],
  SW_U2: [127.300, 60.900, 90],
  SW_U3: [128.500, 68.600, 90],
  J_PX: [21.270, 5.895, 180],
  FB_DF: [107.000, 42.100, 0],
  D_DF: [104.050, 40.050, 270],
  C_DF: [106.350, 31.850, 90],
  C_DFH: [109.050, 39.400, 270],
  R_DFT: [113.600, 32.050, 0],
  J_SPK: [121.300, 5.895, 180],
  J_LO: [125.225, 44.000, 180],
  R_LL: [117.550, 40.500, 270],
  C_LL: [116.850, 44.400, 0],
  R_LR: [114.300, 40.350, 180],
  C_LR: [113.400, 43.350, 270],
  R_X1: [61.000, 23.600, 0],
  R_X2: [67.750, 23.500, 0],
  R_X3: [64.250, 24.450, 90],
  R_X4: [67.750, 25.800, 0],
  R_X5: [71.050, 25.100, 90],
  R_X6: [73.850, 24.400, 90],
  R_X7: [81.750, 24.050, 90],
  R_X8: [78.500, 25.900, 0],
  R_X9: [77.150, 23.350, 0],
  R_X10: [82.700, 26.700, 0],
  R_X11: [84.750, 24.050, 90],
  SJ_V: [58.000, 18.100, 270],
  J_RAIL: [84.700, 5.895, 180],
  TP1: [97.400, 37.300, 0],
  TP2: [23.605, 71.300, 0],   // 2026-10-21: beside the moved C_BO1 +5V pad (was 36.9, 70.6)
  TP3: [51.750, 19.000, 270],
  TP4: [113.250, 22.050, 0],
  TP5: [36.400, 65.000, 180],   // 2026-10-21: in the buck ground strip, clear of the +5V vias and L_BK (was 36.9, 73.3)
  // poses solved after the nest move (routed build reviewed 2026-10-15); frozen so later edits do not move the parts the locked
  // PIXEL / +5V copper ends on
  Q_PX: [12.950, 15.200, 90],
  R_PXG: [7.050, 16.100, 180],
  R_PXU: [10.250, 14.850, 270],
  R_PXS: [16.200, 14.800, 90],
  D_MCU: [22.000, 37.550, 180],
  C_MCU: [25.450, 37.050, 90],
  // 2026-10-20: a new part (the since-removed earth jumper) moved D_C1 (region-placed, not frozen until then) 0.15 mm west; locked at its reviewed
  // rev B pose so the locked +5V copper ends on it
  D_C1: [57.450, 47.400, 180],
};
