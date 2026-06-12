import {computePinPosition} from '../../src/utils/pinPositioning';
import {Layout} from '../../src/constants/layout';

describe('computePinPosition', () => {
  const dims = {width: 800, height: 600};

  it('places a center pin at the image center minus anchor offset', () => {
    const pos = computePinPosition(0.5, 0.5, dims);
    expect(pos.left).toBe(400 - Layout.pinSize * Layout.pinAnchorX);
    expect(pos.top).toBe(300 - Layout.pinSize * Layout.pinAnchorY);
  });

  it('places a top-left pin near (0, 0)', () => {
    const pos = computePinPosition(0, 0, dims);
    expect(pos.left).toBe(-Layout.pinSize * Layout.pinAnchorX);
    expect(pos.top).toBe(-Layout.pinSize * Layout.pinAnchorY);
  });

  it('places a bottom-right pin near the edge', () => {
    const pos = computePinPosition(1, 1, dims);
    expect(pos.left).toBe(800 - Layout.pinSize * Layout.pinAnchorX);
    expect(pos.top).toBe(600 - Layout.pinSize * Layout.pinAnchorY);
  });
});
