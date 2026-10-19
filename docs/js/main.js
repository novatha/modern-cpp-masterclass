/**
 * GridGuard N-2 — Interactive Web Scripts
 * Powers the live topology viewer, physics simulation, contingency trip preview,
 * and clipboard copy utilities.
 */

document.addEventListener('DOMContentLoaded', function () {
  initTopologyViewer();
  initCitationCopy();
});

// ==============================================================================
// 1. LIVE TOPOLOGY VIEWER WITH VIS.JS
// ==============================================================================
var networkInstance = null;
var physicsActive = true;
var currentTripped = false;

var baseNodesData = [
  { id: 1, label: "Bus 1 (Slack)", color: { background: "#ef4444", border: "#ffffff" }, size: 26, title: "<b>Bus #1 [Slack]</b><br>Tegangan: 1.060 p.u.<br>Pg: 232.4 MW | Qg: -16.9 MVAr" },
  { id: 2, label: "Bus 2 (PV)", color: { background: "#f97316", border: "#ffffff" }, size: 22, title: "<b>Bus #2 [PV Gen]</b><br>Tegangan: 1.045 p.u.<br>Pd: 21.7 MW | Pg: 40.0 MW" },
  { id: 3, label: "Bus 3 (PV)", color: { background: "#f97316", border: "#ffffff" }, size: 22, title: "<b>Bus #3 [PV Gen]</b><br>Tegangan: 1.010 p.u.<br>Pd: 94.2 MW" },
  { id: 4, label: "Bus 4 (PQ)", color: { background: "#3b82f6", border: "#ffffff" }, size: 16, title: "<b>Bus #4 [PQ Load]</b><br>Tegangan: 1.019 p.u.<br>Pd: 47.8 MW" },
  { id: 5, label: "Bus 5 (PQ)", color: { background: "#3b82f6", border: "#ffffff" }, size: 16, title: "<b>Bus #5 [PQ Load]</b><br>Tegangan: 1.020 p.u.<br>Pd: 7.6 MW" },
  { id: 6, label: "Bus 6 (PV)", color: { background: "#f97316", border: "#ffffff" }, size: 22, title: "<b>Bus #6 [PV Gen]</b><br>Tegangan: 1.070 p.u.<br>Pd: 11.2 MW" },
  { id: 7, label: "Bus 7 (PQ)", color: { background: "#3b82f6", border: "#ffffff" }, size: 16, title: "<b>Bus #7 [PQ Load]</b><br>Tegangan: 1.062 p.u.<br>Pd: 0.0 MW" },
  { id: 8, label: "Bus 8 (PV)", color: { background: "#f97316", border: "#ffffff" }, size: 22, title: "<b>Bus #8 [PV Gen]</b><br>Tegangan: 1.090 p.u.<br>Pd: 0.0 MW" },
  { id: 9, label: "Bus 9 (PQ)", color: { background: "#3b82f6", border: "#ffffff" }, size: 16, title: "<b>Bus #9 [PQ Load]</b><br>Tegangan: 1.056 p.u.<br>Pd: 29.5 MW" },
  { id: 10, label: "Bus 10 (PQ)", color: { background: "#3b82f6", border: "#ffffff" }, size: 16, title: "<b>Bus #10 [PQ Load]</b><br>Tegangan: 1.051 p.u.<br>Pd: 9.0 MW" },
  { id: 11, label: "Bus 11 (PQ)", color: { background: "#3b82f6", border: "#ffffff" }, size: 16, title: "<b>Bus #11 [PQ Load]</b><br>Tegangan: 1.057 p.u.<br>Pd: 3.5 MW" },
  { id: 12, label: "Bus 12 (PQ)", color: { background: "#3b82f6", border: "#ffffff" }, size: 16, title: "<b>Bus #12 [PQ Load]</b><br>Tegangan: 1.055 p.u.<br>Pd: 6.1 MW" },
  { id: 13, label: "Bus 13 (PQ)", color: { background: "#3b82f6", border: "#ffffff" }, size: 16, title: "<b>Bus #13 [PQ Load]</b><br>Tegangan: 1.050 p.u.<br>Pd: 13.5 MW" },
  { id: 14, label: "Bus 14 (PQ)", color: { background: "#3b82f6", border: "#ffffff" }, size: 16, title: "<b>Bus #14 [PQ Load]</b><br>Tegangan: 1.036 p.u.<br>Pd: 14.9 MW" }
];

var baseEdgesData = [
  { id: "e1", from: 1, to: 2, label: "#1", length: 110, width: 2.5, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #1: Bus 1 ➔ Bus 2 (Z = 0.062 p.u.)" },
  { id: "e2", from: 1, to: 5, label: "#2", length: 180, width: 2.5, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #2: Bus 1 ➔ Bus 5 (Z = 0.229 p.u.)" },
  { id: "e3", from: 2, to: 3, label: "#3", length: 170, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #3: Bus 2 ➔ Bus 3" },
  { id: "e4", from: 2, to: 4, label: "#4", length: 160, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #4: Bus 2 ➔ Bus 4" },
  { id: "e5", from: 2, to: 5, label: "#5", length: 160, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #5: Bus 2 ➔ Bus 5" },
  { id: "e6", from: 3, to: 4, label: "#6", length: 160, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #6: Bus 3 ➔ Bus 4" },
  { id: "e7", from: 4, to: 5, label: "#7", length: 100, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #7: Bus 4 ➔ Bus 5" },
  { id: "e8", from: 4, to: 7, label: "#8", length: 170, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #8: Bus 4 ➔ Bus 7" },
  { id: "e9", from: 4, to: 9, label: "#9", length: 280, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #9: Bus 4 ➔ Bus 9" },
  { id: "e10", from: 5, to: 6, label: "#10", length: 190, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #10: Bus 5 ➔ Bus 6" },
  { id: "e11", from: 6, to: 11, label: "#11", length: 180, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #11: Bus 6 ➔ Bus 11" },
  { id: "e12", from: 6, to: 12, label: "#12", length: 200, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #12: Bus 6 ➔ Bus 12" },
  { id: "e13", from: 6, to: 13, label: "#13", length: 150, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #13: Bus 6 ➔ Bus 13" },
  { id: "e14", from: 7, to: 8, label: "#14", length: 160, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #14: Bus 7 ➔ Bus 8" },
  { id: "e15", from: 7, to: 9, label: "#15", length: 130, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #15: Bus 7 ➔ Bus 9" },
  { id: "e16", from: 9, to: 10, label: "#16", length: 120, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #16: Bus 9 ➔ Bus 10" },
  { id: "e17", from: 9, to: 14, label: "#17", length: 210, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #17: Bus 9 ➔ Bus 14" },
  { id: "e18", from: 10, to: 11, label: "#18", length: 170, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #10: Bus 10 ➔ Bus 11" },
  { id: "e19", from: 12, to: 13, label: "#19", length: 210, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #19: Bus 12 ➔ Bus 13" },
  { id: "e20", from: 13, to: 14, label: "#20", length: 240, width: 2, color: { color: "#64748b", highlight: "#00f0ff" }, title: "Line #20: Bus 13 ➔ Bus 14" }
];

var nodesDataSet = null;
var edgesDataSet = null;

function initTopologyViewer() {
  var container = document.getElementById('network-canvas');
  if (!container || typeof vis === 'undefined') return;

  nodesDataSet = new vis.DataSet(baseNodesData);
  edgesDataSet = new vis.DataSet(baseEdgesData);

  var data = {
    nodes: nodesDataSet,
    edges: edgesDataSet
  };

  var options = {
    physics: {
      enabled: true,
      barnesHut: {
        gravitationalConstant: -3800,
        centralGravity: 0.25,
        springLength: 120,
        springConstant: 0.04,
        damping: 0.12,
        avoidOverlap: 1.0
      },
      stabilization: {
        enabled: true,
        iterations: 250
      }
    },
    interaction: {
      hover: true,
      tooltipDelay: 100,
      zoomView: true,
      dragView: true,
      dragNodes: true
    },
    nodes: {
      borderWidth: 2,
      font: { color: "#ffffff", face: "Inter, sans-serif", size: 12, bold: true },
      shadow: { enabled: true, color: "rgba(0,0,0,0.6)", size: 6, x: 2, y: 2 }
    },
    edges: {
      font: { color: "#94a3b8", size: 10, align: "top" },
      smooth: { type: "continuous", roundness: 0.1 }
    }
  };

  networkInstance = new vis.Network(container, data, options);

  // Setup toolbar buttons
  var btnReset = document.getElementById('btn-reset-zoom');
  if (btnReset) {
    btnReset.addEventListener('click', function () {
      if (networkInstance) networkInstance.fit({ animation: { duration: 600, easingFunction: 'easeInOutQuad' } });
    });
  }

  var btnPhysics = document.getElementById('btn-toggle-physics');
  if (btnPhysics) {
    btnPhysics.addEventListener('click', function () {
      physicsActive = !physicsActive;
      networkInstance.setOptions({ physics: { enabled: physicsActive } });
      btnPhysics.textContent = physicsActive ? "Freeze Layout" : "Activate Physics";
    });
  }

  var btnTrip = document.getElementById('btn-simulate-trip');
  if (btnTrip) {
    btnTrip.addEventListener('click', function () {
      simulateN2Contingency();
    });
  }
}

function simulateN2Contingency() {
  if (!edgesDataSet) return;

  var statusBadge = document.getElementById('demo-status-pill');

  if (!currentTripped) {
    // Trip Line #1 and Line #2 (Islanding Bus #1)
    edgesDataSet.update([
      { id: "e1", color: { color: "#ef4444" }, dashes: true, width: 3, label: "TRIPPED" },
      { id: "e2", color: { color: "#ef4444" }, dashes: true, width: 3, label: "TRIPPED" }
    ]);
    nodesDataSet.update([
      { id: 1, color: { background: "#7f1d1d", border: "#ef4444" }, label: "Bus 1 [ISLAND]" }
    ]);
    if (statusBadge) {
      statusBadge.textContent = "⚠️ KONTINGENSI AKTIF: SALURAN #1 & #2 TRIP (ISLANDING)";
      statusBadge.style.background = "rgba(239, 68, 68, 0.2)";
      statusBadge.style.color = "#f87171";
      statusBadge.style.borderColor = "rgba(239, 68, 68, 0.4)";
    }
    currentTripped = true;
    var btn = document.getElementById('btn-simulate-trip');
    if (btn) btn.textContent = "Pulihkan Saluran (Restore)";
  } else {
    // Restore Lines
    edgesDataSet.update([
      { id: "e1", color: { color: "#64748b" }, dashes: false, width: 2.5, label: "#1" },
      { id: "e2", color: { color: "#64748b" }, dashes: false, width: 2.5, label: "#2" }
    ]);
    nodesDataSet.update([
      { id: 1, color: { background: "#ef4444", border: "#ffffff" }, label: "Bus 1 (Slack)" }
    ]);
    if (statusBadge) {
      statusBadge.textContent = "● SISTEM NORMAL (KONTINU)";
      statusBadge.style.background = "rgba(16, 185, 129, 0.15)";
      statusBadge.style.color = "#10b981";
      statusBadge.style.borderColor = "rgba(16, 185, 129, 0.3)";
    }
    currentTripped = false;
    var btn = document.getElementById('btn-simulate-trip');
    if (btn) btn.textContent = "Simulasi N-2 Trip (#1 & #2)";
  }
}

// ==============================================================================
// 2. BIBTEX CITATION COPY UTILITY
// ==============================================================================
function initCitationCopy() {
  var copyBtn = document.getElementById('btn-copy-bibtex');
  var codeBlock = document.getElementById('bibtex-code');
  if (!copyBtn || !codeBlock) return;

  copyBtn.addEventListener('click', function () {
    var text = codeBlock.innerText;
    navigator.clipboard.writeText(text).then(function () {
      var origText = copyBtn.textContent;
      copyBtn.textContent = "Tersalin! ✓";
      copyBtn.style.color = "#10b981";
      copyBtn.style.borderColor = "#10b981";
      setTimeout(function () {
        copyBtn.textContent = origText;
        copyBtn.style.color = "";
        copyBtn.style.borderColor = "";
      }, 2500);
    });
  });
}
