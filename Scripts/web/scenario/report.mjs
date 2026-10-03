// Scripts/web/scenario/report.mjs: the scenario's rows, as the probes and the page's self-test print theirs:
//   PASS|FAIL|NOTE     scenario <group>.<row>: <detail>
// A NOTE is not counted. The last line is summary(): `scenario: N/M passed`.
export class Report {
  constructor(print = console.log) {
    this.print = print;
    this.group = '';
    this.passed = 0;
    this.failed = 0;
    this.failedHere = 0;                              // in the current group
    this.red = [];                                    // the failing rows' names
  }

  enter(group) {
    this.group = group;
    this.failedHere = 0;
  }

  // A row is one line, whatever its detail quotes (an error's text has its stack's lines).
  line(word, name, detail) {
    const text = (typeof detail === 'string' ? detail : JSON.stringify(detail)).trim().replace(/\s*\n\s*/g, ' | ');
    this.print(`${word.padEnd(8)} scenario ${this.group ? this.group + '.' : ''}${name}${text ? ': ' + text : ''}`);
  }

  row(ok, name, detail = '') {
    this.line(ok ? 'PASS' : 'FAIL', name, detail);
    if (ok) {
      this.passed += 1;
    } else {
      this.failed += 1;
      this.failedHere += 1;
      this.red.push(`${this.group}.${name}`);
    }
    return !!ok;
  }

  note(name, detail = '') {
    this.line('NOTE', name, detail);
  }

  summary() {
    return `scenario: ${this.passed}/${this.passed + this.failed} passed`;
  }
}

// A number as a row says it: at most `digits` decimals, no trailing zeros.
export const num = (value, digits = 2) => (typeof value === 'number' ? String(Number(value.toFixed(digits))) : '?');
