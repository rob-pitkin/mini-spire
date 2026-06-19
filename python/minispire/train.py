"""Train a MaskablePPO agent on Minispire-v0 (ROB-53).

Loads a validated YAML config, builds a vectorized env, trains MaskablePPO
with W&B logging and checkpointing, and saves a final model.

Run:
    minispire-train --config configs/baseline_sparse.yaml
    minispire-train --config configs/baseline_shaped.yaml

W&B: log in first (`wandb login`) or set WANDB_MODE=offline / WANDB_MODE=
disabled to run without a network/account.

Reproducibility note: the seed seeds the env, policy init, numpy, and torch.
Same-machine same-seed runs are close but NOT bit-for-bit reproducible —
SB3 + vec envs + torch carry nondeterminism (thread scheduling, CUDA). The
resolved config and git SHA are logged to W&B for traceability.
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess

import numpy as np
from sb3_contrib import MaskablePPO
from stable_baselines3.common.callbacks import BaseCallback, CheckpointCallback
from stable_baselines3.common.vec_env import DummyVecEnv

from minispire.env import MinispireEnv
from minispire.train_config import TrainConfig


def _git_sha() -> str:
    """Best-effort current git SHA for run traceability; '' if unavailable."""
    try:
        return (
            subprocess.check_output(["git", "rev-parse", "HEAD"], stderr=subprocess.DEVNULL)
            .decode()
            .strip()
        )
    except Exception:
        return ""


class MetricsCallback(BaseCallback):
    """Aggregate task metrics from completed-episode infos and log them.

    Reads ROB-58's per-episode info fields (won / hp_fraction) plus episode
    length from the Monitor-style `episode` info, maintains rolling windows,
    and logs win rate, mean HP retained on win, and mean episode length to
    the SB3 logger (which WandbCallback forwards to W&B).
    """

    def __init__(self, window: int = 100):
        super().__init__()
        self._window = window
        self._wins: list[int] = []
        self._hp_on_win: list[float] = []
        self._lengths: list[int] = []

    def _on_step(self) -> bool:
        # `infos` is one dict per env for this step.
        for info, done in zip(self.locals["infos"], self.locals["dones"]):
            if not done:
                continue
            won = bool(info.get("won", False))
            self._wins.append(1 if won else 0)
            if won:
                self._hp_on_win.append(float(info.get("hp_fraction", 0.0)))
            # SB3's Monitor wrapper adds an "episode" dict with length "l".
            ep = info.get("episode")
            if ep is not None:
                self._lengths.append(int(ep["l"]))

            self._wins = self._wins[-self._window :]
            self._hp_on_win = self._hp_on_win[-self._window :]
            self._lengths = self._lengths[-self._window :]

        if self._wins:
            self.logger.record("rollout/win_rate", float(np.mean(self._wins)))
        if self._hp_on_win:
            self.logger.record(
                "rollout/mean_hp_retained_on_win", float(np.mean(self._hp_on_win))
            )
        if self._lengths:
            self.logger.record("rollout/mean_ep_length", float(np.mean(self._lengths)))
        return True


def make_env_fn(hp_reward_coeff: float):
    """Return a thunk that builds a Monitor-wrapped MinispireEnv.

    Monitor records episode return/length into the info dict, which the
    MetricsCallback consumes.
    """
    from stable_baselines3.common.monitor import Monitor

    def _thunk():
        return Monitor(MinispireEnv(hp_reward_coeff=hp_reward_coeff))

    return _thunk


def train(config: TrainConfig) -> str:
    """Run a training job. Returns the path to the saved final model."""
    import torch

    # Seed everything we can. Not bit-exact across machines (see module docs).
    np.random.seed(config.seed)
    torch.manual_seed(config.seed)

    # --- W&B (optional / offline-safe) ---
    run = None
    callbacks: list[BaseCallback] = []
    try:
        import wandb
        from wandb.integration.sb3 import WandbCallback

        run = wandb.init(
            project=config.wandb_project,
            name=config.run_name,
            config={**config.model_dump(), "git_sha": _git_sha()},
            sync_tensorboard=True,
            save_code=False,
        )
        callbacks.append(WandbCallback(verbose=1))
        run_id = run.id
    except Exception as exc:  # noqa: BLE001 — W&B is optional
        print(f"warning: W&B unavailable ({exc}); continuing without it.")
        run_id = config.run_name

    # --- Vectorized env (DummyVecEnv; MinispireEnv exposes action_masks) ---
    vec_env = DummyVecEnv(
        [make_env_fn(config.hp_reward_coeff) for _ in range(config.n_envs)]
    )
    vec_env.seed(config.seed)

    # --- Model ---
    # Only enable tensorboard logging when both a live W&B run and the
    # tensorboard package are available (WandbCallback syncs the TB logs).
    # In offline/disabled mode or without tensorboard installed, skip it so
    # training still runs.
    tb_log = None
    if run is not None:
        try:
            import tensorboard  # noqa: F401

            tb_log = f"runs/{run_id}"
        except ImportError:
            print("warning: tensorboard not installed; skipping TB logging.")

    model = MaskablePPO(
        "MlpPolicy",
        vec_env,
        learning_rate=config.learning_rate,
        n_steps=config.n_steps,
        batch_size=config.batch_size,
        n_epochs=config.n_epochs,
        gamma=config.gamma,
        seed=config.seed,
        tensorboard_log=tb_log,
        verbose=1,
    )

    # --- Callbacks ---
    callbacks.append(MetricsCallback())
    ckpt_dir = pathlib.Path("checkpoints") / run_id
    if config.save_freq > 0:
        callbacks.append(
            CheckpointCallback(
                save_freq=max(config.save_freq // config.n_envs, 1),
                save_path=str(ckpt_dir),
                name_prefix="ppo",
            )
        )

    model.learn(total_timesteps=config.total_timesteps, callback=callbacks)

    # --- Final save ---
    ckpt_dir.mkdir(parents=True, exist_ok=True)
    final_path = ckpt_dir / "final.zip"
    model.save(str(final_path))
    print(f"saved final model to {final_path}")

    vec_env.close()
    if run is not None:
        run.finish()
    return str(final_path)


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="minispire-train",
        description="Train a MaskablePPO agent on Minispire-v0.",
    )
    parser.add_argument(
        "--config", required=True, help="Path to a training config YAML."
    )
    args = parser.parse_args()

    config = TrainConfig.from_yaml(args.config)
    train(config)


if __name__ == "__main__":
    main()
