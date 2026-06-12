import {Layout} from '@/constants/layout';

export interface RenderedImageDimensions {
  width: number;
  height: number;
}

export interface PinScreenPosition {
  left: number;
  top: number;
}

export function computePinPosition(
  xRatio: number,
  yRatio: number,
  rendered: RenderedImageDimensions,
): PinScreenPosition {
  return {
    left: xRatio * rendered.width - Layout.pinSize * Layout.pinAnchorX,
    top: yRatio * rendered.height - Layout.pinSize * Layout.pinAnchorY,
  };
}
