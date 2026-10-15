import { useState } from "react";
import Sim from "./Sim";
import Controls from "./Controls";
import type { SimModule } from "./simModule";

export default function App() {
  const [sim, setSim] = useState<SimModule | null>(null);
  const [open, setOpen] = useState(false);

  return (
    <div className="stage">
      <Sim onReady={setSim} />

      <div className="menu">
        <button
          className="menu-head"
          aria-expanded={open}
          onClick={(e) => { setOpen((o) => !o); e.currentTarget.blur(); }}
        >
          <span className={`menu-chev ${open ? "open" : ""}`}>▸</span>
          {open ? "Hide controls" : "Show controls"}
        </button>

        {/* Controls stays mounted so slider state and polling survive hiding */}
        <div className={`menu-body ${open ? "open" : ""}`} inert={!open}>
          <div className="menu-inner">
            <div className="menu-scroll">
              <Controls sim={sim} />
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}