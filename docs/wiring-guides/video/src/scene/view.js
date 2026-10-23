// A camera for step data: look at `target` from `dist` mm away, `az` degrees round from the front (positive: from the
// right), `el` degrees up. Returns [px, py, pz, tx, ty, tz].
export const view = (target, dist, az = 0, el = 35) => {
  const a = (az * Math.PI) / 180, e = (el * Math.PI) / 180;
  return [target[0] + dist * Math.cos(e) * Math.sin(a), target[1] + dist * Math.sin(e), target[2] + dist * Math.cos(e) * Math.cos(a), ...target];
};
