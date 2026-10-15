export type SimModule = {
  _setStop(v: number): void;
  _getStop(): number;
  _requestStep(): void;
  _setVorticityStrength(v: number): void;
  _getVorticityStrength(): number;
  _setFlipRatio(v: number): void;
  _getFlipRatio(): number;
  _setGravityX(v: number): void;
  _getGravityX(): number;
  _setGravityY(v: number): void;
  _getGravityY(): number;
  _setFireActive(v: number): void;
  _getFireActive(): number;
  _setRenderMode(m: number): void;
  _getRenderMode(): number;
  _setActiveObject(o: number): void;
  _getActiveObject(): number;
  _setBrushSize(v: number): void;
  _getBrushSize(): number;
  _setForceStrength(v: number): void;
  _getForceStrength(): number;
};

declare global {
  interface Window {
    createSim?: (opts: object) => Promise<SimModule>;
  }
}