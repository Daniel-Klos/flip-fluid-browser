import { useEffect, useState, type ReactNode } from "react";
import type { SimModule } from "./simModule";
import "./Controls.css";

const RENDER_MODES = ["Diffusion", "Velocity", "Vorticity", "Temperature", "Pressure"];
const TEMPERATURE_MODE = 3;
const OBJECTS = ["Drag", "Attract", "Repel", "Spawn Particles", "Delete Particles", "Draw", "Erase", "Zoom / drag"];

function Section(props: { title: string; defaultOpen?: boolean; children: ReactNode }) {
  const [open, setOpen] = useState(props.defaultOpen ?? false);
  return (
    <div className="sec">
      <button
        className="sec-head"
        aria-expanded={open}
        onClick={(e) => { setOpen((o) => !o); e.currentTarget.blur(); }}
      >
        <span className={`chev ${open ? "open" : ""}`}>▸</span>
        {props.title}
      </button>
      <div className={`sec-body ${open ? "open" : ""}`}>
        <div className="sec-inner">
          <div className="sec-content">{props.children}</div>
        </div>
      </div>
    </div>
  );
}

function NumberField(props: {
  value: number; min: number; max: number; step: number;
  disabled: boolean; onCommit: (v: number) => void;
}) {
  const { value, min, max, step, disabled, onCommit } = props;
  const [draft, setDraft] = useState<string | null>(null); // null = not editing

  const decimals = (String(step).split(".")[1] ?? "").length;
  const allowNeg = min < 0;
  const allowDot = decimals > 0;
  const format = (v: number) => v.toFixed(decimals);
  const clampRound = (v: number) =>
    Number(Math.min(max, Math.max(min, v)).toFixed(decimals));

  const finish = () => {
    if (draft !== null) {
      const n = parseFloat(draft);
      if (!Number.isNaN(n)) onCommit(clampRound(n));
    }
    setDraft(null); // empty or invalid input just reverts to the current value
  };

  return (
    <input
      className="num"
      type="text"
      inputMode={allowDot ? "decimal" : "numeric"}
      disabled={disabled}
      value={draft ?? format(value)}
      onFocus={(e) => e.currentTarget.select()}
      onChange={(e) => {
        const s = e.target.value;
        const pattern = allowNeg ? /^-?\d*\.?\d*$/ : /^\d*\.?\d*$/;
        if (!pattern.test(s) || (!allowDot && s.includes("."))) return; // reject bad input
        setDraft(s);
        const n = parseFloat(s);
        if (!Number.isNaN(n) && n >= min && n <= max) onCommit(n); // live update when valid
      }}
      onBlur={finish}
      onKeyDown={(e) => {
        e.stopPropagation(); // keep sim hotkeys from firing while typing
        if (e.key === "Enter") e.currentTarget.blur();
        if (e.key === "Escape") { setDraft(null); e.currentTarget.blur(); }
      }}
      onKeyUp={(e) => e.stopPropagation()}
    />
  );
}

function SliderRow(props: {
  label: string; min: number; max: number; step: number;
  value: number; onChange: (v: number) => void; disabled: boolean;
}) {
  return (
    <div className="row">
      <span className="row-label">{props.label}</span>
      <input
        type="range" min={props.min} max={props.max} step={props.step}
        value={props.value} disabled={props.disabled}
        onChange={(e) => props.onChange(Number(e.target.value))}
        onPointerUp={(e) => e.currentTarget.blur()}
      />
      <NumberField
        value={props.value} min={props.min} max={props.max} step={props.step}
        disabled={props.disabled} onCommit={props.onChange}
      />
    </div>
  );
}

export default function Controls({ sim }: { sim: SimModule | null }) {
  const [vorticity, setVorticity] = useState(0.0);
  const [flip, setFlip] = useState(0.0);
  const [visc, setVisc] = useState(1);  
  const [gravX, setGravX] = useState(0.0);
  const [gravY, setGravY] = useState(-9.81);
  const [brush, setBrush] = useState(50);
  const [force, setForce] = useState(1);
  const [stopped, setStopped] = useState(false);
  const [fire, setFire] = useState(false);
  const [mode, setMode] = useState(0);
  const [object, setObject] = useState(0);

  // Read the sim's real defaults once loaded
  useEffect(() => {
    if (!sim) return;
    setVorticity(sim._getVorticityStrength());
    setFlip(sim._getFlipRatio());
    setVisc(1 - sim._getApicRatio());
    setGravX(sim._getGravityX());
    setGravY(sim._getGravityY());
    setBrush(sim._getBrushSize());
    setForce(sim._getForceStrength());
  }, [sim]);

  // Poll values that C++ hotkeys can change
  useEffect(() => {
    if (!sim) return;
    const id = setInterval(() => {
      setStopped(!!sim._getStop());
      setFire(!!sim._getFireActive());
      setMode(sim._getRenderMode());
      setObject(sim._getActiveObject());
    }, 100);
    return () => clearInterval(id);
  }, [sim]);

  const disabled = !sim;
  const blur = (e: React.SyntheticEvent) => (e.currentTarget as HTMLElement).blur();

  return (
    <div className="controls">
      <Section title="Interaction" defaultOpen>
        <div className="row">
          <span className="row-label">Pointer Action</span>
          <select
            value={object} disabled={disabled}
            onChange={(e) => { const o = Number(e.target.value); setObject(o); sim?._setActiveObject(o); blur(e); }}
          >
            {OBJECTS.map((name, i) => <option key={i} value={i}>{name}</option>)}
          </select>
        </div>
        <SliderRow label="Brush Size" min={1} max={1000} step={1} value={brush} disabled={disabled}
          onChange={(v) => { setBrush(v); sim?._setBrushSize(v); }} />
        <SliderRow label="Force Strength" min={0} max={400} step={1} value={force} disabled={disabled}
          onChange={(v) => { setForce(v); sim?._setForceStrength(v); }} />
      </Section>

      <Section title="Simulation" defaultOpen>
        <div className="btn-row">
          <button className="btn" disabled={disabled}
            onClick={(e) => { const n = !stopped; setStopped(n); sim?._setStop(n ? 1 : 0); blur(e); }}>
            {stopped ? "Resume" : "Pause"}
          </button>
          <button className="btn" disabled={disabled || !stopped}
            onClick={(e) => { sim?._requestStep(); blur(e); }}>
            Step
          </button>
          <button className="btn" disabled={disabled || (mode != TEMPERATURE_MODE)}
            onClick={(e) => { const n = !fire; setFire(n); sim?._setFireActive(n ? 1 : 0); blur(e); }}>
            {fire ? "Stop fire" : "Start fire"}
          </button>
        </div>
        <div className="row">
          <span className="row-label">Render Mode</span>
          <select
            value={mode} disabled={disabled}
            onChange={(e) => { const m = Number(e.target.value); setMode(m); sim?._setRenderMode(m); blur(e); }}
          >
            {RENDER_MODES.map((name, i) => <option key={i} value={i}>{name}</option>)}
          </select>
        </div>
        <SliderRow label="Vorticity" min={0} max={500} step={0.1} value={vorticity} disabled={disabled}
          onChange={(v) => { setVorticity(v); sim?._setVorticityStrength(v); }} />
        <SliderRow label="Viscosity" min={0} max={1} step={0.01} value={visc} disabled={disabled}
          onChange={(v) => { setVisc(v); sim?._setApicRatio(1 - v); }} />
        <SliderRow label="Liveliness" min={0} max={1} step={0.01} value={flip} disabled={disabled}
          onChange={(v) => { setFlip(v); sim?._setFlipRatio(v); }} />
        <SliderRow label="Gravity X" min={-40} max={40} step={0.1} value={gravX} disabled={disabled}
          onChange={(v) => { setGravX(v); sim?._setGravityX(v); }} />
        <SliderRow label="Gravity Y" min={-40} max={40} step={0.1} value={gravY} disabled={disabled}
          onChange={(v) => { setGravY(v); sim?._setGravityY(v); }} />
      </Section>
    </div>
  );
}