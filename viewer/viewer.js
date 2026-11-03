/**
 * RouteBudget Lab — Phase 8 Interactive MAPF Replay Viewer
 * Vanilla HTML5 Canvas visualizer for verified multi-agent pathfinding trajectories.
 * Zero external CDN dependencies.
 */

(function () {
  "use strict";

  // Application State
  const state = {
    catalog: null,
    currentReplay: null,
    currentStep: 0,
    makespan: 0,
    isPlaying: false,
    playInterval: null,
    speedMultiplier: 1.0,
    baseFps: 8,
    selectedAgentId: null,
    
    // Viewport transform (pan & zoom)
    view: {
      scale: 1.0,
      panX: 0,
      panY: 0,
      cellSize: 16,
      isDragging: false,
      dragStartX: 0,
      dragStartY: 0
    },

    // Display options
    display: {
      showTrails: true,
      showLabels: true,
      showMarkers: true,
      showGrid: true,
      stayAtGoal: true,
      agentSize: "auto"
    }
  };

  // DOM Elements
  const els = {
    mapSelect: document.getElementById("mapSelect"),
    scenSelect: document.getElementById("scenSelect"),
    agentsSelect: document.getElementById("agentsSelect"),
    budgetSelect: document.getElementById("budgetSelect"),
    methodSelect: document.getElementById("methodSelect"),
    protoSelect: document.getElementById("protoSelect"),
    loadBtn: document.getElementById("loadBtn"),
    validationBadge: document.getElementById("validationBadge"),
    modeBadge: document.getElementById("modeBadge"),

    canvasContainer: document.getElementById("canvasContainer"),
    canvas: document.getElementById("mapCanvas"),
    hudMapDims: document.getElementById("hudMapDims"),
    hudHoverCoords: document.getElementById("hudHoverCoords"),
    hudZoom: document.getElementById("hudZoom"),
    hudStep: document.getElementById("hudStep"),
    hudMakespan: document.getElementById("hudMakespan"),
    hudSolveTime: document.getElementById("hudSolveTime"),
    hudBudget: document.getElementById("hudBudget"),
    fitViewBtn: document.getElementById("fitViewBtn"),
    resetViewBtn: document.getElementById("resetViewBtn"),

    timelineSlider: document.getElementById("timelineSlider"),
    timelineStepLabel: document.getElementById("timelineStepLabel"),
    timelineMaxLabel: document.getElementById("timelineMaxLabel"),
    playBtn: document.getElementById("playBtn"),
    stepBackBtn: document.getElementById("stepBackBtn"),
    stepFwdBtn: document.getElementById("stepFwdBtn"),
    resetBtn: document.getElementById("resetBtn"),
    speedSelect: document.getElementById("speedSelect"),
    playbackStatusText: document.getElementById("playbackStatusText"),

    metaRunId: document.getElementById("metaRunId"),
    metaMethod: document.getElementById("metaMethod"),
    metaProtocol: document.getElementById("metaProtocol"),
    metaSeed: document.getElementById("metaSeed"),
    metaSha: document.getElementById("metaSha"),
    mFinalCost: document.getElementById("mFinalCost"),
    mMakespan: document.getElementById("mMakespan"),
    mSolveTime: document.getElementById("mSolveTime"),
    mOvershoot: document.getElementById("mOvershoot"),
    mTotalAgents: document.getElementById("mTotalAgents"),
    mActiveAgents: document.getElementById("mActiveAgents"),
    mGoalAgents: document.getElementById("mGoalAgents"),
    mCoordConvention: document.getElementById("mCoordConvention"),

    agentSelect: document.getElementById("agentSelect"),
    agentColorDot: document.getElementById("agentColorDot"),
    agentTitle: document.getElementById("agentTitle"),
    agentStatusBadge: document.getElementById("agentStatusBadge"),
    agentStart: document.getElementById("agentStart"),
    agentGoal: document.getElementById("agentGoal"),
    agentCurrent: document.getElementById("agentCurrent"),
    agentLen: document.getElementById("agentLen"),
    agentArrival: document.getElementById("agentArrival"),
    agentChipGrid: document.getElementById("agentChipGrid"),

    toggleTrails: document.getElementById("toggleTrails"),
    toggleLabels: document.getElementById("toggleLabels"),
    toggleMarkers: document.getElementById("toggleMarkers"),
    toggleGrid: document.getElementById("toggleGrid"),
    toggleStayAtGoal: document.getElementById("toggleStayAtGoal"),
    agentSizeSelect: document.getElementById("agentSizeSelect")
  };

  const ctx = els.canvas.getContext("2d");

  // Agent Color Generator (Golden Ratio HSL for optimal visual separation)
  function getAgentColor(agentId, alpha = 1.0) {
    const hue = (agentId * 137.508) % 360;
    return `hsla(${hue}, 85%, 60%, ${alpha})`;
  }

  function getAgentHighlightColor(agentId) {
    const hue = (agentId * 137.508) % 360;
    return `hsl(${hue}, 100%, 75%)`;
  }

  // 1. Initialize & Fetch Catalog
  async function init() {
    setupEventListeners();
    resizeCanvas();
    window.addEventListener("resize", () => {
      resizeCanvas();
      draw();
    });

    try {
      // Fetch index from server API or relative path
      let resp = await fetch("/api/replays/index").catch(() => null);
      if (!resp || !resp.ok) {
        resp = await fetch("../results/phase8/replays/replays_index.json");
      }
      if (!resp.ok) throw new Error("Could not load replays index");
      state.catalog = await resp.json();
      populateFilters(state.catalog.filters, state.catalog.replays);
      
      // Auto-load first replay
      if (state.catalog.replays.length > 0) {
        loadReplayBySummary(state.catalog.replays[0]);
      }
    } catch (err) {
      console.error("Initialization error:", err);
      els.playbackStatusText.textContent = "Failed to load replays index.";
    }
  }

  // 2. Filter Dropdown Management
  function populateFilters(filters, replays) {
    populateSelect(els.mapSelect, filters.maps);
    populateSelect(els.scenSelect, filters.scenarios, s => `Scenario ${s}`);
    populateSelect(els.agentsSelect, filters.agent_counts, a => `${a} Agents`);
    populateSelect(els.budgetSelect, filters.budgets, b => `${b.toFixed(1)}s Budget`);
    populateSelect(els.methodSelect, filters.methods);
    populateSelect(els.protoSelect, filters.protocols);

    // Event listener for filter changes
    [els.mapSelect, els.scenSelect, els.agentsSelect, els.budgetSelect, els.methodSelect, els.protoSelect].forEach(sel => {
      sel.addEventListener("change", () => {
        highlightMatchingReplay();
      });
    });
  }

  function populateSelect(selectEl, values, labelFn = v => v) {
    selectEl.innerHTML = "";
    values.forEach(v => {
      const opt = document.createElement("option");
      opt.value = v;
      opt.textContent = labelFn(v);
      selectEl.appendChild(opt);
    });
  }

  function findMatchingReplay() {
    if (!state.catalog || !state.catalog.replays) return null;
    const m = els.mapSelect.value;
    const s = parseInt(els.scenSelect.value, 10);
    const a = parseInt(els.agentsSelect.value, 10);
    const b = parseFloat(els.budgetSelect.value);
    const meth = els.methodSelect.value;
    const proto = els.protoSelect.value;

    return state.catalog.replays.find(r => 
      r.map_name === m &&
      r.scenario_id === s &&
      r.agent_count === a &&
      Math.abs(r.budget_s - b) < 0.01 &&
      r.method === meth &&
      r.protocol === proto
    ) || null;
  }

  function highlightMatchingReplay() {
    const r = findMatchingReplay();
    if (r) {
      els.loadBtn.classList.add("btn-primary");
      els.loadBtn.title = "Load selected replay";
      els.playbackStatusText.textContent = `Ready to load: ${r.run_id}`;
      els.playbackStatusText.style.color = "";
    } else {
      els.loadBtn.classList.remove("btn-primary");
      els.loadBtn.title = "No replay matches this exact filter combination";
      els.playbackStatusText.textContent = "No replay matches selected filters.";
      els.playbackStatusText.style.color = "#f85149";
    }
  }

  // 3. Load Replay JSON
  async function loadReplayBySummary(summary) {
    pause();
    els.playbackStatusText.style.color = "";
    els.playbackStatusText.textContent = `Loading ${summary.run_id}...`;

    try {
      let resp = await fetch(`/api/replays/${summary.replay_file}`).catch(() => null);
      if (!resp || !resp.ok) {
        resp = await fetch(`../results/phase8/replays/${summary.replay_file}`);
      }
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const data = await resp.json();
      setLoadedReplay(data);
    } catch (err) {
      console.error("Failed to load replay:", err);
      els.playbackStatusText.textContent = `Error loading replay: ${err.message}`;
      els.playbackStatusText.style.color = "#f85149";
    }
  }

  function setLoadedReplay(replayData) {
    state.currentReplay = replayData;
    state.currentStep = 0;
    state.makespan = replayData.metrics.makespan;
    state.selectedAgentId = 0;

    // Sync select dropdowns with loaded data
    els.mapSelect.value = replayData.provenance.map_name;
    els.scenSelect.value = replayData.provenance.scenario_id;
    els.agentsSelect.value = replayData.provenance.agent_count;
    els.budgetSelect.value = replayData.provenance.budget_s;
    els.methodSelect.value = replayData.provenance.method;
    els.protoSelect.value = replayData.provenance.timing_protocol;

    // Timeline setup
    els.timelineSlider.min = 0;
    els.timelineSlider.max = state.makespan;
    els.timelineSlider.value = 0;
    els.timelineStepLabel.textContent = `Step 0`;
    els.timelineMaxLabel.textContent = `Step ${state.makespan}`;

    // HUD & Badges
    els.validationBadge.textContent = replayData.metrics.validation_status;
    els.validationBadge.className = `badge ${replayData.metrics.validation_status === "VALID" ? "badge-success" : "badge-danger"}`;
    els.hudMapDims.textContent = `Map: ${replayData.map.name} (${replayData.map.width} × ${replayData.map.height})`;
    els.hudStep.textContent = "0";
    els.hudMakespan.textContent = state.makespan;
    els.hudSolveTime.textContent = `${replayData.metrics.total_solve_time_s.toFixed(3)}s`;
    els.hudBudget.textContent = `${replayData.provenance.budget_s.toFixed(1)}s`;

    // Sidebar Meta Tab
    els.metaRunId.textContent = replayData.run_id;
    els.metaMethod.textContent = replayData.provenance.method;
    els.metaProtocol.textContent = replayData.provenance.timing_protocol;
    els.metaSeed.textContent = replayData.provenance.seed;
    els.metaSha.textContent = replayData.provenance.source_paths_sha256 ? replayData.provenance.source_paths_sha256.substring(0, 16) + "..." : "—";
    els.metaSha.title = replayData.provenance.source_paths_sha256;

    els.mFinalCost.textContent = replayData.metrics.final_cost.toLocaleString();
    els.mMakespan.textContent = replayData.metrics.makespan;
    els.mSolveTime.textContent = `${replayData.metrics.total_solve_time_s.toFixed(4)}s`;
    
    const overUs = replayData.metrics.deadline_overshoot_s * 1e6;
    els.mOvershoot.textContent = overUs > 1.0 ? `+${(overUs/1000).toFixed(2)} ms` : "0.00 µs (≤1µs tol)";
    els.mOvershoot.style.color = overUs > 1.0 ? "var(--warning)" : "var(--success)";

    els.mTotalAgents.textContent = replayData.agents.length;
    els.mCoordConvention.textContent = `${replayData.provenance.source_coordinate_convention} → Normalized (x,y)`;

    // Agent Inspector
    populateAgentList(replayData.agents);
    selectAgent(0);

    // Initial Viewport Fit
    fitToViewport();
    updatePlaybackMetrics();
    draw();
    els.playbackStatusText.textContent = `Loaded ${replayData.run_id}`;
  }

  function populateAgentList(agents) {
    els.agentSelect.innerHTML = "";
    els.agentChipGrid.innerHTML = "";

    agents.forEach(a => {
      // Dropdown option
      const opt = document.createElement("option");
      opt.value = a.id;
      opt.textContent = `Agent #${a.id} (len: ${a.path_length}, arr: ${a.arrival_step})`;
      els.agentSelect.appendChild(opt);

      // Quick chip
      const chip = document.createElement("div");
      chip.className = `agent-chip ${a.id === state.selectedAgentId ? "selected" : ""}`;
      chip.id = `chip_agent_${a.id}`;
      chip.textContent = `#${a.id}`;
      chip.style.borderLeft = `3px solid ${getAgentColor(a.id)}`;
      chip.addEventListener("click", () => selectAgent(a.id));
      els.agentChipGrid.appendChild(chip);
    });
  }

  function selectAgent(agentId) {
    state.selectedAgentId = agentId;
    els.agentSelect.value = agentId;

    document.querySelectorAll(".agent-chip").forEach(c => c.classList.remove("selected"));
    const selectedChip = document.getElementById(`chip_agent_${agentId}`);
    if (selectedChip) selectedChip.classList.add("selected");

    updateAgentInspector();
    draw();
  }

  function updateAgentInspector() {
    if (!state.currentReplay || state.selectedAgentId === null) return;
    const a = state.currentReplay.agents[state.selectedAgentId];
    if (!a) return;

    els.agentColorDot.style.backgroundColor = getAgentColor(a.id);
    els.agentTitle.textContent = `Agent #${a.id}`;
    els.agentStart.textContent = `(${a.start[0]}, ${a.start[1]})`;
    els.agentGoal.textContent = `(${a.goal[0]}, ${a.goal[1]})`;

    const pos = getAgentPosition(a, state.currentStep);
    els.agentCurrent.textContent = `(${pos[0]}, ${pos[1]})`;
    els.agentLen.textContent = `${a.path_length} steps`;
    els.agentArrival.textContent = `Step ${a.arrival_step}`;

    const isAtGoal = (state.currentStep >= a.arrival_step);
    els.agentStatusBadge.textContent = isAtGoal ? "At Goal" : "Moving";
    els.agentStatusBadge.className = `badge ${isAtGoal ? "badge-success" : "badge-neutral"}`;
  }

  // 4. Position & Simulation Engine
  function getAgentPosition(agent, step) {
    if (!agent || !agent.path || agent.path.length === 0) return [0, 0];
    if (step < agent.path.length) {
      return agent.path[step];
    }
    // Stay-at-goal simulation behavior: stays at final path cell
    if (state.display.stayAtGoal) {
      return agent.path[agent.path.length - 1];
    }
    return agent.path[agent.path.length - 1];
  }

  function updatePlaybackMetrics() {
    if (!state.currentReplay) return;
    els.hudStep.textContent = state.currentStep;
    els.timelineStepLabel.textContent = `Step ${state.currentStep}`;
    els.timelineSlider.value = state.currentStep;

    let activeCount = 0;
    let goalCount = 0;
    state.currentReplay.agents.forEach(a => {
      if (state.currentStep >= a.arrival_step) {
        goalCount++;
      } else {
        activeCount++;
      }
    });

    els.mActiveAgents.textContent = activeCount;
    els.mGoalAgents.textContent = `${goalCount} / ${state.currentReplay.agents.length}`;
    updateAgentInspector();
  }

  // 5. Canvas Drawing & Viewport
  function resizeCanvas() {
    const rect = els.canvasContainer.getBoundingClientRect();
    els.canvas.width = rect.width;
    els.canvas.height = rect.height;
  }

  function fitToViewport() {
    if (!state.currentReplay) return;
    const mapW = state.currentReplay.map.width;
    const mapH = state.currentReplay.map.height;
    const pad = 40;

    const availW = els.canvas.width - pad;
    const availH = els.canvas.height - pad;

    const scaleX = availW / mapW;
    const scaleY = availH / mapH;
    state.view.cellSize = Math.max(2, Math.min(scaleX, scaleY));

    // Center map
    state.view.panX = (els.canvas.width - (mapW * state.view.cellSize)) / 2;
    state.view.panY = (els.canvas.height - (mapH * state.view.cellSize)) / 2;
    state.view.scale = 1.0;

    updateZoomHUD();
    draw();
  }

  function updateZoomHUD() {
    const totalZoom = Math.round((state.view.cellSize * state.view.scale / 16) * 100);
    els.hudZoom.textContent = `Zoom: ${totalZoom}%`;
  }

  function draw() {
    ctx.clearRect(0, 0, els.canvas.width, els.canvas.height);

    if (!state.currentReplay) {
      ctx.fillStyle = "#8b949e";
      ctx.font = "14px " + getComputedStyle(document.body).fontFamily;
      ctx.textAlign = "center";
      ctx.fillText("Select a verified replay run above to inspect trajectories", els.canvas.width / 2, els.canvas.height / 2);
      return;
    }

    ctx.save();
    // Apply viewport transform
    ctx.translate(state.view.panX, state.view.panY);
    ctx.scale(state.view.scale, state.view.scale);

    const cs = state.view.cellSize;
    const map = state.currentReplay.map;
    const mapW = map.width * cs;
    const mapH = map.height * cs;

    // 1. Draw Map Background & Grid
    ctx.fillStyle = "#0d1117";
    ctx.fillRect(0, 0, mapW, mapH);

    // Optional fine grid lines
    if (state.display.showGrid && cs * state.view.scale >= 8) {
      ctx.strokeStyle = "rgba(48, 54, 61, 0.4)";
      ctx.lineWidth = 0.5;
      ctx.beginPath();
      for (let x = 0; x <= map.width; x++) {
        ctx.moveTo(x * cs, 0);
        ctx.lineTo(x * cs, mapH);
      }
      for (let y = 0; y <= map.height; y++) {
        ctx.moveTo(0, y * cs);
        ctx.lineTo(mapW, y * cs);
      }
      ctx.stroke();
    }

    // 2. Draw Obstacles (@ and T)
    ctx.fillStyle = "#21262d";
    ctx.strokeStyle = "#30363d";
    ctx.lineWidth = 0.5;

    for (let y = 0; y < map.height; y++) {
      const row = map.grid[y];
      for (let x = 0; x < map.width; x++) {
        const ch = row[x];
        if (ch === "@" || ch === "T") {
          ctx.fillRect(x * cs, y * cs, cs, cs);
          if (cs >= 6) {
            ctx.strokeRect(x * cs, y * cs, cs, cs);
          }
        }
      }
    }

    // Map boundary border
    ctx.strokeStyle = "#58a6ff";
    ctx.lineWidth = 1.5;
    ctx.strokeRect(0, 0, mapW, mapH);

    // 3. Draw Path Trails (Optional)
    if (state.display.showTrails) {
      state.currentReplay.agents.forEach(a => {
        const isSelected = (a.id === state.selectedAgentId);
        // Only draw non-selected agents with subtle alpha, selected agent bold
        if (!isSelected && state.selectedAgentId !== null && a.id !== state.selectedAgentId) {
          // Dim non-selected if one is picked
          drawAgentTrail(a, cs, false, 0.15);
        } else {
          drawAgentTrail(a, cs, isSelected, isSelected ? 0.9 : 0.4);
        }
      });
    }

    // 4. Draw Start & Goal Markers (Optional)
    if (state.display.showMarkers) {
      state.currentReplay.agents.forEach(a => {
        const isSelected = (a.id === state.selectedAgentId);
        const col = getAgentColor(a.id);

        // Start marker (ring)
        const sx = a.start[0] * cs + cs / 2;
        const sy = a.start[1] * cs + cs / 2;
        ctx.strokeStyle = isSelected ? "#fff" : col;
        ctx.lineWidth = isSelected ? 2.5 : 1.2;
        ctx.beginPath();
        ctx.arc(sx, sy, cs * 0.35, 0, Math.PI * 2);
        ctx.stroke();

        // Goal marker (square target)
        const gx = a.goal[0] * cs + cs * 0.15;
        const gy = a.goal[1] * cs + cs * 0.15;
        const gw = cs * 0.7;
        ctx.strokeStyle = isSelected ? "#fff" : col;
        ctx.lineWidth = isSelected ? 2.0 : 1.0;
        ctx.strokeRect(gx, gy, gw, gw);

        // Diagonal cross in goal
        ctx.beginPath();
        ctx.moveTo(gx, gy);
        ctx.lineTo(gx + gw, gy + gw);
        ctx.moveTo(gx + gw, gy);
        ctx.lineTo(gx, gy + gw);
        ctx.stroke();
      });
    }

    // 5. Draw Agents at currentStep
    const radiusMultiplier = getRadiusMultiplier();
    const baseRadius = Math.max(2, (cs * 0.4) * radiusMultiplier);

    state.currentReplay.agents.forEach(a => {
      const isSelected = (a.id === state.selectedAgentId);
      const pos = getAgentPosition(a, state.currentStep);
      const cx = pos[0] * cs + cs / 2;
      const cy = pos[1] * cs + cs / 2;
      const isAtGoal = (state.currentStep >= a.arrival_step);
      const col = getAgentColor(a.id);

      // Selected agent luminous halo
      if (isSelected) {
        ctx.strokeStyle = "#58a6ff";
        ctx.lineWidth = 3;
        ctx.beginPath();
        ctx.arc(cx, cy, baseRadius + 4, 0, Math.PI * 2);
        ctx.stroke();
      }

      // Agent body
      ctx.fillStyle = isSelected ? getAgentHighlightColor(a.id) : col;
      ctx.beginPath();
      ctx.arc(cx, cy, baseRadius, 0, Math.PI * 2);
      ctx.fill();

      // Border
      ctx.strokeStyle = isAtGoal ? "#3fb950" : "#ffffff";
      ctx.lineWidth = isAtGoal ? 2 : 1;
      ctx.stroke();

      // Goal glow if arrived
      if (isAtGoal) {
        ctx.strokeStyle = "rgba(63, 185, 80, 0.4)";
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(cx, cy, baseRadius + 2, 0, Math.PI * 2);
        ctx.stroke();
      }

      // Agent labels (if enabled and cell size supports it)
      if (state.display.showLabels && cs * state.view.scale >= 12) {
        ctx.fillStyle = "#ffffff";
        ctx.font = `bold ${Math.max(8, Math.floor(baseRadius * 1.1))}px ` + getComputedStyle(document.body).fontFamily;
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(a.id, cx, cy);
      }
    });

    ctx.restore();
  }

  function drawAgentTrail(agent, cs, isSelected, alpha) {
    if (!agent.path || agent.path.length < 2) return;

    const col = getAgentColor(agent.id, alpha);
    ctx.strokeStyle = isSelected ? getAgentHighlightColor(agent.id) : col;
    ctx.lineWidth = isSelected ? 2.5 : 1.2;

    // Past trail (from 0 to currentStep)
    ctx.beginPath();
    const pastEnd = Math.min(state.currentStep, agent.path.length - 1);
    ctx.moveTo(agent.path[0][0] * cs + cs / 2, agent.path[0][1] * cs + cs / 2);
    for (let t = 1; t <= pastEnd; t++) {
      ctx.lineTo(agent.path[t][0] * cs + cs / 2, agent.path[t][1] * cs + cs / 2);
    }
    ctx.stroke();

    // Future trail (dashed from currentStep to end)
    if (state.currentStep < agent.path.length - 1) {
      ctx.save();
      ctx.setLineDash([3, 3]);
      ctx.strokeStyle = getAgentColor(agent.id, alpha * 0.6);
      ctx.beginPath();
      ctx.moveTo(agent.path[pastEnd][0] * cs + cs / 2, agent.path[pastEnd][1] * cs + cs / 2);
      for (let t = pastEnd + 1; t < agent.path.length; t++) {
        ctx.lineTo(agent.path[t][0] * cs + cs / 2, agent.path[t][1] * cs + cs / 2);
      }
      ctx.stroke();
      ctx.restore();
    }
  }

  function getRadiusMultiplier() {
    switch (state.display.agentSize) {
      case "small": return 0.65;
      case "large": return 1.35;
      default: return 1.0;
    }
  }

  // 6. Playback Engine
  function play() {
    if (state.isPlaying) return;
    if (state.currentStep >= state.makespan) {
      state.currentStep = 0;
    }
    state.isPlaying = true;
    els.playBtn.textContent = "⏸ Pause";
    els.playBtn.classList.add("btn-pause");
    els.playbackStatusText.textContent = "Playing...";

    const intervalMs = Math.max(16, Math.floor(1000 / (state.baseFps * state.speedMultiplier)));
    state.playInterval = setInterval(() => {
      if (state.currentStep < state.makespan) {
        state.currentStep++;
        updatePlaybackMetrics();
        draw();
      } else {
        pause();
        els.playbackStatusText.textContent = "Completed";
      }
    }, intervalMs);
  }

  function pause() {
    if (!state.isPlaying) return;
    state.isPlaying = false;
    clearInterval(state.playInterval);
    state.playInterval = null;
    els.playBtn.textContent = "▶ Play";
    els.playBtn.classList.remove("btn-pause");
    els.playbackStatusText.textContent = "Paused";
  }

  function togglePlay() {
    if (state.isPlaying) {
      pause();
    } else {
      play();
    }
  }

  function stepForward() {
    pause();
    if (state.currentStep < state.makespan) {
      state.currentStep++;
      updatePlaybackMetrics();
      draw();
    }
  }

  function stepBackward() {
    pause();
    if (state.currentStep > 0) {
      state.currentStep--;
      updatePlaybackMetrics();
      draw();
    }
  }

  function reset() {
    pause();
    state.currentStep = 0;
    updatePlaybackMetrics();
    draw();
    els.playbackStatusText.textContent = "Reset to Step 0";
  }

  function setStep(newStep) {
    state.currentStep = Math.max(0, Math.min(state.makespan, parseInt(newStep, 10)));
    updatePlaybackMetrics();
    draw();
  }

  // 7. Event Listeners & Interaction
  function setupEventListeners() {
    // Top Controls
    els.loadBtn.addEventListener("click", () => {
      const match = findMatchingReplay();
      if (match) {
        loadReplayBySummary(match);
      } else {
        els.playbackStatusText.textContent = "Cannot load: No replay matches selected filters.";
        els.playbackStatusText.style.color = "#f85149";
      }
    });

    els.fitViewBtn.addEventListener("click", fitToViewport);
    els.resetViewBtn.addEventListener("click", () => {
      state.view.panX = 0;
      state.view.panY = 0;
      state.view.scale = 1.0;
      updateZoomHUD();
      draw();
    });

    // Playback Controls
    els.playBtn.addEventListener("click", togglePlay);
    els.stepFwdBtn.addEventListener("click", stepForward);
    els.stepBackBtn.addEventListener("click", stepBackward);
    els.resetBtn.addEventListener("click", reset);

    els.timelineSlider.addEventListener("input", e => {
      pause();
      setStep(e.target.value);
    });

    els.speedSelect.addEventListener("change", e => {
      state.speedMultiplier = parseFloat(e.target.value);
      if (state.isPlaying) {
        pause();
        play();
      }
    });

    // Keyboard Shortcuts
    window.addEventListener("keydown", e => {
      if (e.target.tagName === "SELECT" || e.target.tagName === "INPUT") return;
      if (e.code === "Space") {
        e.preventDefault();
        togglePlay();
      } else if (e.code === "ArrowRight") {
        e.preventDefault();
        stepForward();
      } else if (e.code === "ArrowLeft") {
        e.preventDefault();
        stepBackward();
      } else if (e.code === "Home") {
        e.preventDefault();
        reset();
      } else if (e.key === "f" || e.key === "F") {
        fitToViewport();
      }
    });

    // Canvas Pan (Drag)
    els.canvasContainer.addEventListener("mousedown", e => {
      if (e.button === 0) {
        state.view.isDragging = true;
        state.view.dragStartX = e.clientX - state.view.panX;
        state.view.dragStartY = e.clientY - state.view.panY;
      }
    });

    window.addEventListener("mousemove", e => {
      if (state.view.isDragging) {
        state.view.panX = e.clientX - state.view.dragStartX;
        state.view.panY = e.clientY - state.view.dragStartY;
        draw();
      }

      // Mouse Coordinates Tracker
      if (state.currentReplay) {
        const rect = els.canvas.getBoundingClientRect();
        const mx = (e.clientX - rect.left - state.view.panX) / (state.view.cellSize * state.view.scale);
        const my = (e.clientY - rect.top - state.view.panY) / (state.view.cellSize * state.view.scale);
        const gx = Math.floor(mx);
        const gy = Math.floor(my);
        if (gx >= 0 && gx < state.currentReplay.map.width && gy >= 0 && gy < state.currentReplay.map.height) {
          els.hudHoverCoords.textContent = `Coord: (${gx}, ${gy})`;
        } else {
          els.hudHoverCoords.textContent = "Coord: (—, —)";
        }
      }
    });

    window.addEventListener("mouseup", () => {
      state.view.isDragging = false;
    });

    // Canvas Zoom (Scroll wheel)
    els.canvasContainer.addEventListener("wheel", e => {
      e.preventDefault();
      const zoomFactor = e.deltaY < 0 ? 1.15 : 0.87;
      const rect = els.canvas.getBoundingClientRect();
      const mouseX = e.clientX - rect.left;
      const mouseY = e.clientY - rect.top;

      // Zoom centered at mouse position
      state.view.panX = mouseX - (mouseX - state.view.panX) * zoomFactor;
      state.view.panY = mouseY - (mouseY - state.view.panY) * zoomFactor;
      state.view.scale *= zoomFactor;

      updateZoomHUD();
      draw();
    }, { passive: false });

    // Agent Click Selection
    els.canvas.addEventListener("click", e => {
      if (!state.currentReplay) return;
      const rect = els.canvas.getBoundingClientRect();
      const cs = state.view.cellSize * state.view.scale;
      const clickX = e.clientX - rect.left - state.view.panX;
      const clickY = e.clientY - rect.top - state.view.panY;

      // Find closest agent within threshold radius
      let closestAgent = null;
      let closestDist = Infinity;
      const threshold = Math.max(12, cs * 0.8);

      state.currentReplay.agents.forEach(a => {
        const pos = getAgentPosition(a, state.currentStep);
        const ax = pos[0] * cs + cs / 2;
        const ay = pos[1] * cs + cs / 2;
        const dist = Math.hypot(clickX - ax, clickY - ay);
        if (dist <= threshold && dist < closestDist) {
          closestDist = dist;
          closestAgent = a;
        }
      });

      if (closestAgent) {
        selectAgent(closestAgent.id);
      }
    });

    // Agent Selector Dropdown
    els.agentSelect.addEventListener("change", e => {
      selectAgent(parseInt(e.target.value, 10));
    });

    // Display Toggles
    els.toggleTrails.addEventListener("change", e => {
      state.display.showTrails = e.target.checked;
      draw();
    });

    els.toggleLabels.addEventListener("change", e => {
      state.display.showLabels = e.target.checked;
      draw();
    });

    els.toggleMarkers.addEventListener("change", e => {
      state.display.showMarkers = e.target.checked;
      draw();
    });

    els.toggleGrid.addEventListener("change", e => {
      state.display.showGrid = e.target.checked;
      draw();
    });

    els.toggleStayAtGoal.addEventListener("change", e => {
      state.display.stayAtGoal = e.target.checked;
      updatePlaybackMetrics();
      draw();
    });

    els.agentSizeSelect.addEventListener("change", e => {
      state.display.agentSize = e.target.value;
      draw();
    });

    // Sidebar Tab Switching
    document.querySelectorAll(".tab-btn").forEach(btn => {
      btn.addEventListener("click", () => {
        document.querySelectorAll(".tab-btn").forEach(b => b.classList.remove("active"));
        document.querySelectorAll(".tab-content").forEach(c => c.classList.remove("active"));
        btn.classList.add("active");
        const targetTab = document.getElementById(btn.getAttribute("data-tab"));
        if (targetTab) targetTab.classList.add("active");
      });
    });
  }

  // Start app on DOM load
  window.addEventListener("DOMContentLoaded", init);
})();
