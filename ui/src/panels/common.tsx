// 面板共用小组件。

export function Vec3Input(props: {
  values: string[];
  onChange: (index: number, value: string) => void;
  disabled?: boolean;
}) {
  return (
    <span className="vec3-inputs">
      {props.values.map((v, i) => (
        <input
          key={i}
          type="number"
          step="any"
          value={v}
          disabled={props.disabled}
          onChange={(e) => props.onChange(i, e.target.value)}
        />
      ))}
    </span>
  );
}

export function parseVec3(values: string[]): number[] | null {
  const nums = values.map((v) => Number(v));
  return nums.every((n) => Number.isFinite(n)) ? nums : null;
}

export function fmtVec(values: number[] | undefined, digits = 2): string {
  if (!values) return '-';
  return values.map((v) => (+v).toFixed(digits)).join(', ');
}
