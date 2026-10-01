module.exports = function(minified) {
  var clayConfig = this;
  var _ = minified._;
  var $ = minified.$;
  var HTML = minified.HTML;

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var historyStr = clayConfig.meta.userData && clayConfig.meta.userData.historyStr;
    if (!historyStr) historyStr = localStorage.getItem("pushup_history");

    if (!historyStr || historyStr === "[]") {
      var btn = document.getElementById('btn-export-csv');
      if (btn) btn.style.display = 'none';
      return;
    }

    try {
      var history = JSON.parse(historyStr);
      if (!history || history.length === 0) {
        var btn = document.getElementById('btn-export-csv');
        if (btn) btn.style.display = 'none';
        return;
      }

      // Exercise ids match the watch app's ExerciseType enum
      var EXERCISES = [
        { name: "Push-ups", color: "#FF4700" },
        { name: "Pull-ups", color: "#0055AA" },
        { name: "Squats",   color: "#00AA55" }
      ];

      // Group by exercise and date (YYYY-MM-DD) for charting.
      // Entries logged before multi-exercise support have no `ex` and are push-ups.
      var dailyTotals = [];
      for (var x = 0; x < EXERCISES.length; x++) dailyTotals.push({});
      for (var i = 0; i < history.length; i++) {
        var entry = history[i];
        var totals = dailyTotals[entry.ex || 0];
        if (!totals) continue;
        var d = new Date(entry.time * 1000);
        var dateStr = d.getFullYear() + "-" + (d.getMonth()+1) + "-" + d.getDate();
        if (!totals[dateStr]) {
          totals[dateStr] = 0;
        }
        totals[dateStr] += entry.count;
      }

      var currentLabels = [];
      var currentDatasets = [];
      var activeTimeframe = null;
      var activeViewMode = null;
      var activeExercise = null;

      function renderDashboard() {
        if (activeTimeframe === null) {
          try { activeTimeframe = localStorage.getItem('dashboard_timeframe') || '7'; } catch(e) { activeTimeframe = '7'; }
        }
        if (activeViewMode === null) {
          try { activeViewMode = localStorage.getItem('dashboard_view') || 'chart'; } catch(e) { activeViewMode = 'chart'; }
        }
        if (activeExercise === null) {
          try { activeExercise = localStorage.getItem('dashboard_exercise') || 'all'; } catch(e) { activeExercise = 'all'; }
        }
        var shownExercises = [];
        for (var x = 0; x < EXERCISES.length; x++) {
          if (activeExercise === 'all' || activeExercise === String(x)) shownExercises.push(x);
        }

        var timeframe = activeTimeframe;
        var viewMode = activeViewMode;

        var dates = [];
        var today = new Date();
        today.setHours(0, 0, 0, 0);

        if (timeframe !== 'all') {
          var limit = parseInt(timeframe, 10);
          for (var i = limit - 1; i >= 0; i--) {
            var d = new Date(today.getTime() - i * 24 * 60 * 60 * 1000);
            var dateStr = d.getFullYear() + "-" + (d.getMonth() + 1) + "-" + d.getDate();
            dates.push(dateStr);
          }
        } else {
          var firstDate = today;
          if (history.length > 0) {
            firstDate = new Date(history[0].time * 1000);
            firstDate.setHours(0, 0, 0, 0);
          }
          var daysDiff = Math.floor((today.getTime() - firstDate.getTime()) / (1000 * 60 * 60 * 24));
          if (daysDiff > 1825) daysDiff = 1825; 
          if (daysDiff < 0) daysDiff = 0;
          for (var i = daysDiff; i >= 0; i--) {
            var d = new Date(today.getTime() - i * 24 * 60 * 60 * 1000);
            var dateStr = d.getFullYear() + "-" + (d.getMonth() + 1) + "-" + d.getDate();
            dates.push(dateStr);
          }
        }

        currentLabels = [];
        for (var j = 0; j < dates.length; j++) {
          var parts = dates[j].split("-");
          currentLabels.push(parts[2] + "." + parts[1] + ".");
        }
        currentDatasets = [];
        for (var sx = 0; sx < shownExercises.length; sx++) {
          var exId = shownExercises[sx];
          var values = [];
          for (var j2 = 0; j2 < dates.length; j2++) {
            values.push(dailyTotals[exId][dates[j2]] || 0);
          }
          currentDatasets.push({ label: EXERCISES[exId].name, color: EXERCISES[exId].color, values: values });
        }

        var exerciseOptions = '<option value="all"' + (activeExercise==='all'?' selected':'') + '>All Exercises</option>';
        for (var ox = 0; ox < EXERCISES.length; ox++) {
          exerciseOptions += '<option value="' + ox + '"' + (activeExercise===String(ox)?' selected':'') + '>' + EXERCISES[ox].name + '</option>';
        }

        var controlsHTML = '<select id="exercise-select" style="width:100%; margin-bottom:10px; padding:5px; border-radius:4px; font-size:14px;">' +
            exerciseOptions +
          '</select>' +
          '<div style="display:flex; justify-content:space-between; margin-bottom:10px;">' +
          '<select id="timeframe-select" style="flex:1; margin-right:10px; padding:5px; border-radius:4px; font-size:14px;">' +
            '<option value="7"' + (timeframe==='7'?' selected':'') + '>Last 7 Days</option>' +
            '<option value="30"' + (timeframe==='30'?' selected':'') + '>Last 30 Days</option>' +
            '<option value="365"' + (timeframe==='365'?' selected':'') + '>Last Year</option>' +
            '<option value="all"' + (timeframe==='all'?' selected':'') + '>All Time</option>' +
          '</select>' +
          '<select id="view-select" style="flex:1; padding:5px; border-radius:4px; font-size:14px;">' +
            '<option value="chart"' + (viewMode==='chart'?' selected':'') + '>Chart</option>' +
            '<option value="table"' + (viewMode==='table'?' selected':'') + '>Table</option>' +
          '</select>' +
        '</div>';

        var contentHTML = '';

        if (viewMode === 'chart') {
          var iframeHTML = '<!DOCTYPE html>' +
            '<html>' +
            '<head>' +
            '<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">' +
            '<script src="https://cdn.jsdelivr.net/npm/chart.js"></' + 'script>' +
            '<style>' +
            '  body, html { margin:0; padding:0; background:transparent; overflow:hidden; font-family:sans-serif; }' +
            '  canvas { display:block; width:100%; height:180px; }' +
            '</style>' +
            '</head>' +
            '<body>' +
            '<canvas id="chart"></canvas>' +
            '<script>' +
            '  var chart;' +
            '  window.addEventListener("message", function(e) {' +
            '    if(e.data && e.data.type === "render") {' +
            '      if(chart) chart.destroy();' +
            '      var ctx = document.getElementById("chart").getContext("2d");' +
            '      var datasets = e.data.datasets.map(function(ds) {' +
            '        return { label: ds.label, data: ds.values, backgroundColor: ds.color, borderRadius: 4 };' +
            '      });' +
            '      chart = new Chart(ctx, {' +
            '        type: "bar",' +
            '        data: {' +
            '          labels: e.data.labels,' +
            '          datasets: datasets' +
            '        },' +
            '        options: {' +
            '          responsive: true,' +
            '          maintainAspectRatio: false,' +
            '          plugins: { legend: { display: datasets.length > 1, labels: { boxWidth: 12 } } },' +
            '          scales: {' +
            '            y: { stacked: true, beginAtZero: true, ticks: { precision: 0 } },' +
            '            x: { stacked: true, grid: { display: false } }' +
            '          }' +
            '        }' +
            '      });' +
            '    }' +
            '  });' +
            '  window.parent.postMessage("chartReady", "*");' +
            '</' + 'script>' +
            '</body>' +
            '</html>';

          var encodedSrc = "data:text/html;charset=utf-8," + encodeURIComponent(iframeHTML.trim());
          contentHTML = '<iframe id="chart-iframe" src="' + encodedSrc + '" style="width:100%; height:180px; border:none; margin-bottom:10px;"></iframe>';
        } else {
          contentHTML = '<div style="max-height:180px; overflow-y:auto; border:1px solid #ccc; border-radius:4px; margin-bottom:10px;">' +
            '<table style="width:100%; border-collapse:collapse; font-size:14px; font-family:sans-serif;">' +
            '<thead style="background:#eee; position:sticky; top:0;"><tr><th style="padding:8px; text-align:left; border-bottom:1px solid #ccc;">Date</th>';
          for (var hx = 0; hx < currentDatasets.length; hx++) {
            contentHTML += '<th style="padding:8px; text-align:right; border-bottom:1px solid #ccc;">' + currentDatasets[hx].label + '</th>';
          }
          contentHTML += '</tr></thead><tbody>';
          
          for (var k = dates.length - 1; k >= 0; k--) {
            contentHTML += '<tr><td style="padding:8px; border-bottom:1px solid #eee;">' + currentLabels[k] + '</td>';
            for (var cx = 0; cx < currentDatasets.length; cx++) {
              contentHTML += '<td style="padding:8px; text-align:right; border-bottom:1px solid #eee;">' + currentDatasets[cx].values[k] + '</td>';
            }
            contentHTML += '</tr>';
          }
          contentHTML += '</tbody></table></div>';
        }

        var summaryParts = [];
        for (var tx = 0; tx < currentDatasets.length; tx++) {
          var sum = 0;
          for (var s = 0; s < currentDatasets[tx].values.length; s++) sum += currentDatasets[tx].values[s];
          summaryParts.push(currentDatasets[tx].label + ': ' + sum);
        }

        var summaryHTML = '<p style="font-size:12px; color:#666; text-align:center;">Total in Period &mdash; ' + summaryParts.join(' &middot; ') + '</p>';

        $('#dashboard-chart').set('innerHTML', controlsHTML + contentHTML + summaryHTML);

        var tfSelect = document.getElementById('timeframe-select');
        if (tfSelect) {
          tfSelect.addEventListener('change', function(e) {
            activeTimeframe = e.target.value;
            try { localStorage.setItem('dashboard_timeframe', activeTimeframe); } catch(err) {}
            renderDashboard();
          });
        }
        var exSelect = document.getElementById('exercise-select');
        if (exSelect) {
          exSelect.addEventListener('change', function(e) {
            activeExercise = e.target.value;
            try { localStorage.setItem('dashboard_exercise', activeExercise); } catch(err) {}
            renderDashboard();
          });
        }
        var vwSelect = document.getElementById('view-select');
        if (vwSelect) {
          vwSelect.addEventListener('change', function(e) {
            activeViewMode = e.target.value;
            try { localStorage.setItem('dashboard_view', activeViewMode); } catch(err) {}
            renderDashboard();
          });
        }
      }

      if (!window.__dashboardMessageListenerAdded) {
        window.addEventListener('message', function(e) {
          if(e.data === 'chartReady') {
            var iframe = document.getElementById('chart-iframe');
            if(iframe && iframe.contentWindow) {
              iframe.contentWindow.postMessage({ type: 'render', labels: currentLabels, datasets: currentDatasets }, '*');
            }
          }
        });
        window.__dashboardMessageListenerAdded = true;
      }

      renderDashboard();

      // Setup Export CSV
      $('#btn-export-csv').on('click', function(e) {
        e.preventDefault();
        
        var rawCsvContent = "Date,Timestamp,Exercise,Reps\n";
        for (var n = 0; n < history.length; n++) {
          var row = history[n];
          var rd = new Date(row.time * 1000);
          var exName = EXERCISES[row.ex || 0] ? EXERCISES[row.ex || 0].name : "Unknown";
          rawCsvContent += rd.toISOString() + "," + row.time + "," + exName + "," + row.count + "\n";
        }
        
        // Hide the export button
        var exportBtn = document.getElementById('btn-export-csv');
        if (exportBtn) exportBtn.style.display = 'none';
        
        // Replace dashboard chart with textarea
        var exportHTML = '<div style="margin-bottom:10px;">' +
          '<p style="font-size:14px; font-weight:bold; margin-top:0;">CSV Export (Copy Text below)</p>' +
          '<textarea id="csv-export-area" style="width:100%; height:180px; font-family:monospace; font-size:12px; padding:5px; box-sizing:border-box;">' + rawCsvContent + '</textarea>' +
          '<div style="display:flex; justify-content:flex-end; margin-top:10px;">' +
            '<button class="btn" id="btn-close-export" style="padding:4px 15px; font-size:14px; background-color:#aaa; border:none; border-radius:4px; color:#fff;">Close</button>' +
          '</div>' +
        '</div>';
        
        $('#dashboard-chart').set('innerHTML', exportHTML);
        
        // Auto-select text in textarea
        var textArea = document.getElementById('csv-export-area');
        if (textArea) {
          textArea.focus();
          textArea.select();
          try { textArea.setSelectionRange(0, 99999); } catch(err) {}
        }
        
        // Handle close button
        $('#btn-close-export').on('click', function(ev) {
          ev.preventDefault();
          if (exportBtn) exportBtn.style.display = 'block';
          renderDashboard();
        });
      });

    } catch (e) {
      try {
        $('#dashboard-chart').set('innerHTML', '<div style="color:red; font-size:12px;">Error: ' + e.message + '</div>');
      } catch(e2) {}
      console.log("Error rendering dashboard: " + e);
    }
  });
};
