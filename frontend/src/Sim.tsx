import { useEffect, useRef } from "react";
import type { SimModule } from "./simModule";

let started = false; // guards against React Strict Mode's double-mount in dev

export default function Sim({ onReady }: { onReady: (sim: SimModule) => void }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || started) return;
    started = true;

    const script = document.createElement("script");
    script.src = `${import.meta.env.BASE_URL}sim.js`;
    script.onload = async () => {
      //const mod = await window.createSim!({ canvas: canvasRef.current });
      const mod = await window.createSim!({ canvas });
      onReady(mod);
    };
    document.body.appendChild(script);
  }, [onReady]);

  return (
    <canvas
      ref={canvasRef}
      id="canvas"
      onContextMenu={(e) => e.preventDefault()}
    />
  );
}