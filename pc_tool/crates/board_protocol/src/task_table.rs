//! `vTaskList()` output printed by the `tasks` command:
//! `name<spaces>  state  priority  stack_words  number  [core]`.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TaskRow {
    pub name: String,
    /// X running, B blocked, R ready, S suspended, D deleted.
    pub state: String,
    pub priority: u32,
    pub stack_free_words: u32,
    pub task_number: u32,
    /// -1 = no affinity.
    pub core: Option<i32>,
}

/// Parse the reply lines of `tasks`; header and junk lines are skipped.
pub fn parse_task_table(lines: &[String]) -> Vec<TaskRow> {
    lines
        .iter()
        .filter_map(|line| parse_task_row(line))
        .collect()
}

fn parse_task_row(line: &str) -> Option<TaskRow> {
    let tokens: Vec<&str> = line.split_whitespace().collect();
    if tokens.len() < 5 {
        return None;
    }
    // Try "with core" first (last 5 tokens: state prio stack num core).
    for numeric_count in [4usize, 3] {
        if tokens.len() < numeric_count + 2 {
            continue;
        }
        let state_index = tokens.len() - numeric_count - 1;
        let state = tokens[state_index];
        if state.len() != 1 || !"XBRSD".contains(state) {
            continue;
        }
        let numbers: Option<Vec<i64>> = tokens[state_index + 1..]
            .iter()
            .map(|text| text.parse().ok())
            .collect();
        let Some(numbers) = numbers else { continue };
        let name = tokens[..state_index].join(" ");
        if name.is_empty() {
            continue;
        }
        return Some(TaskRow {
            name,
            state: state.to_string(),
            priority: numbers[0] as u32,
            stack_free_words: numbers[1] as u32,
            task_number: numbers[2] as u32,
            core: numbers.get(3).map(|value| *value as i32),
        });
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_rows_with_and_without_core() {
        let lines: Vec<String> = [
            "name           state prio stack num core",
            "face_logic      X       5       1200    7       1",
            "IDLE0           R       0       300     3       0",
            "console         B       2       2200    9       -1",
            "httpd           B       5       1800    12",
            "Internal error: x",
        ]
        .iter()
        .map(|line| line.to_string())
        .collect();
        let rows = parse_task_table(&lines);
        assert_eq!(rows.len(), 4);
        assert_eq!(rows[0].name, "face_logic");
        assert_eq!(rows[0].state, "X");
        assert_eq!(rows[0].core, Some(1));
        assert_eq!(rows[2].core, Some(-1));
        assert_eq!(rows[3].core, None);
        assert_eq!(rows[3].stack_free_words, 1800);
    }
}
